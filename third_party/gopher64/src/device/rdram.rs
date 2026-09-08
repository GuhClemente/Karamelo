use crate::device;
use crate::retroachievements;
use crate::ui;
use std::alloc::{Layout, alloc_zeroed, dealloc, handle_alloc_error};

/// RDRAM is imported straight into Vulkan as host memory by parallel-rdp, which
/// requires the pointer to be aligned to the driver's
/// `minImportedHostPointerAlignment`. 64 KiB is comfortably above every value
/// seen in the wild, so the buffer is over-aligned to that.
pub const RDRAM_ALIGNMENT: usize = 64 * 1024;

/// An over-aligned byte buffer that deallocates with the layout it was actually
/// allocated with.
///
/// This exists because the obvious way to write it is undefined behaviour, and
/// on Windows it is the kind that actually bites:
///
/// ```ignore
/// let layout = Layout::from_size_align(size, 64 * 1024).unwrap();
/// let ptr = unsafe { alloc_zeroed(layout) };
/// Vec::from_raw_parts(ptr, size, size)   // <-- wrong
/// ```
///
/// `Vec<u8>` always deallocates with `Layout::array::<u8>(capacity)`, i.e.
/// alignment 1 - not the alignment the block was allocated with. `dealloc`
/// requires the *same* layout that `alloc` was given, so this is UB by the
/// letter of the contract, and Windows makes it concrete: `std`'s allocator
/// serves over-aligned requests by over-allocating and stashing the real base
/// pointer just below the aligned one, and only reads that back on free when
/// the layout says the alignment is large. Freeing with alignment 1 skips that
/// step and hands `HeapFree` an address that is not the start of a heap block,
/// which trips the heap's own corruption check (STATUS_HEAP_CORRUPTION,
/// 0xC0000374) inside `Device`'s drop glue - long after the code that caused it.
///
/// Keeping the buffer in a type that remembers its own alignment fixes it at
/// the source. Everything else keeps working through `Deref<Target = [u8]>`.
pub struct AlignedBytes {
    ptr: *mut u8,
    len: usize,
}

// Same reasoning as Vec<u8>: this uniquely owns its allocation and hands out
// no interior references of its own. Needed because Device is held behind an
// Arc<tokio::Mutex<..>> for savestates, which requires Send.
unsafe impl Send for AlignedBytes {}
unsafe impl Sync for AlignedBytes {}

impl AlignedBytes {
    pub fn new_zeroed(len: usize) -> Self {
        if len == 0 {
            return Self::empty();
        }
        let layout = Layout::from_size_align(len, RDRAM_ALIGNMENT).expect("Invalid layout");
        let ptr = unsafe { alloc_zeroed(layout) };
        if ptr.is_null() {
            handle_alloc_error(layout);
        }
        Self { ptr, len }
    }

    /// A buffer that owns nothing. The pointer is never dereferenced (deref
    /// short-circuits on len == 0), it only has to be non-null and aligned.
    pub fn empty() -> Self {
        Self {
            ptr: RDRAM_ALIGNMENT as *mut u8,
            len: 0,
        }
    }
}

impl Drop for AlignedBytes {
    fn drop(&mut self) {
        if self.len == 0 {
            return;
        }
        // Same size AND same alignment as the allocation - the whole point.
        let layout = Layout::from_size_align(self.len, RDRAM_ALIGNMENT).expect("Invalid layout");
        unsafe { dealloc(self.ptr, layout) };
    }
}

impl std::ops::Deref for AlignedBytes {
    type Target = [u8];
    fn deref(&self) -> &[u8] {
        if self.len == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(self.ptr, self.len) }
        }
    }
}

impl std::ops::DerefMut for AlignedBytes {
    fn deref_mut(&mut self) -> &mut [u8] {
        if self.len == 0 {
            &mut []
        } else {
            unsafe { std::slice::from_raw_parts_mut(self.ptr, self.len) }
        }
    }
}

impl Clone for AlignedBytes {
    fn clone(&self) -> Self {
        let mut copy = Self::new_zeroed(self.len);
        copy.copy_from_slice(self);
        copy
    }
}

// Wire format matches what #[derive(Serialize)] produced for Vec<u8> under
// postcard - a varint length followed by the raw bytes - so savestates written
// by an older build still load.
impl serde::Serialize for AlignedBytes {
    fn serialize<S: serde::Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        serializer.serialize_bytes(self)
    }
}

impl<'de> serde::Deserialize<'de> for AlignedBytes {
    fn deserialize<D: serde::Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        struct BytesVisitor;

        impl<'de> serde::de::Visitor<'de> for BytesVisitor {
            type Value = AlignedBytes;

            fn expecting(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
                f.write_str("RDRAM contents as bytes")
            }

            fn visit_bytes<E: serde::de::Error>(self, v: &[u8]) -> Result<Self::Value, E> {
                let mut out = AlignedBytes::new_zeroed(v.len());
                out.copy_from_slice(v);
                Ok(out)
            }

            // Formats that hand bytes back element by element rather than as
            // one borrowed slice.
            fn visit_seq<A: serde::de::SeqAccess<'de>>(
                self,
                mut seq: A,
            ) -> Result<Self::Value, A::Error> {
                let mut tmp: Vec<u8> = Vec::with_capacity(seq.size_hint().unwrap_or(0));
                while let Some(b) = seq.next_element::<u8>()? {
                    tmp.push(b);
                }
                let mut out = AlignedBytes::new_zeroed(tmp.len());
                out.copy_from_slice(&tmp);
                Ok(out)
            }
        }

        deserializer.deserialize_bytes(BytesVisitor)
    }
}

//const RDRAM_CONFIG_REG: usize = 0;
//const RDRAM_DEVICE_ID_REG: usize = 1;
//const RDRAM_DELAY_REG: usize = 2;
const RDRAM_MODE_REG: usize = 3;
//const RDRAM_REF_INTERVAL_REG: usize = 4;
//const RDRAM_REF_ROW_REG: usize = 5;
//const RDRAM_RAS_INTERVAL_REG: usize = 6;
//const RDRAM_MIN_INTERVAL_REG: usize = 7;
//const RDRAM_ADDR_SELECT_REG: usize = 8;
//const RDRAM_DEVICE_MANUF_REG: usize = 9;
pub const RDRAM_REGS_COUNT: usize = 10;

pub const RDRAM_MASK: usize = 0xFFFFFF;

#[derive(Clone, serde::Serialize, serde::Deserialize)]
pub struct Rdram {
    pub mem: AlignedBytes,
    pub size: u32,
    pub regs: [[u32; RDRAM_REGS_COUNT]; 4],
}

pub fn read_mem_fast(
    device: &device::Device,
    address: u64,
    _access_size: device::memory::AccessSize,
) -> u32 {
    let masked_address = address as usize & RDRAM_MASK;
    u32::from_ne_bytes(
        device
            .rdram
            .mem
            .get(masked_address..masked_address + 4)
            .unwrap_or(&[0; 4])
            .try_into()
            .unwrap_or_default(),
    )
}

pub fn read_mem(
    device: &mut device::Device,
    address: u64,
    access_size: device::memory::AccessSize,
) -> u32 {
    device::cop0::add_cycles(
        device,
        rdram_calculate_cycles(access_size as u64) / (access_size as u64 / 4),
    );
    let masked_address = address as usize & RDRAM_MASK;

    ui::video::check_framebuffers(masked_address as u32, 4);

    u32::from_ne_bytes(
        device
            .rdram
            .mem
            .get(masked_address..masked_address + 4)
            .unwrap_or(&[0; 4])
            .try_into()
            .unwrap_or_default(),
    )
}

pub fn write_mem(device: &mut device::Device, address: u64, value: u32, mask: u32) {
    ui::video::check_framebuffers(address as u32, 4);

    let mut data = u32::from_ne_bytes(
        device
            .rdram
            .mem
            .get(address as usize..(address + 4) as usize)
            .unwrap_or(&[0; 4])
            .try_into()
            .unwrap_or_default(),
    );
    device::memory::masked_write_32(&mut data, value, mask);
    device
        .rdram
        .mem
        .get_mut(address as usize..(address + 4) as usize)
        .unwrap_or(&mut [0; 4])
        .copy_from_slice(&data.to_ne_bytes());
}

pub fn write_mem_repeat(device: &mut device::Device, address: u64, value: u32, mask: u32) {
    if mask != 0xFFFFFFFF {
        panic!("RDRAM write_mem_repeat called with mask {:#x}", mask);
    }

    let repeat_length =
        (device.mi.regs[device::mi::MI_INIT_MODE_REG] & device::mi::MI_INIT_LENGTH_MASK) + 1;

    if !repeat_length.is_multiple_of(4) {
        panic!(
            "RDRAM write_mem_repeat called with non-word-aligned length {}",
            repeat_length
        );
    }

    ui::video::check_framebuffers(address as u32, repeat_length);

    for i in (0..repeat_length as u64).step_by(4) {
        device
            .rdram
            .mem
            .get_mut((address + i) as usize..(address + i) as usize + 4)
            .unwrap_or(&mut [0; 4])
            .copy_from_slice(&value.to_ne_bytes());
    }

    device.mi.regs[device::mi::MI_INIT_MODE_REG] &= !device::mi::MI_INIT_MODE;
    for i in 0..(0x3F00000 >> 16) {
        device.memory.memory_map_write[i] = device::rdram::write_mem;
    }
}

pub fn read_regs(
    device: &mut device::Device,
    address: u64,
    _access_size: device::memory::AccessSize,
) -> u32 {
    device::cop0::add_cycles(device, 20);
    let chip_id = (address >> 13) & 3;
    let reg = (address & 0x3FF) >> 2;
    match reg as usize {
        RDRAM_MODE_REG => device.pi.regs[reg as usize] ^ 0xc0c0c0c0,
        0x80 => 0x00000000, //Row, needed for libdragon
        _ => device.rdram.regs[chip_id as usize][reg as usize],
    }
}

pub fn write_regs(device: &mut device::Device, address: u64, value: u32, mask: u32) {
    let chip_id = (address >> 13) & 3;
    let reg = (address & 0x3FF) >> 2;
    device::memory::masked_write_32(
        &mut device.rdram.regs[chip_id as usize][reg as usize],
        value,
        mask,
    )
}

pub fn init(device: &mut device::Device) {
    // See AlignedBytes: the old code allocated with a 64 KiB-aligned Layout and
    // then handed the pointer to Vec::from_raw_parts, which deallocates with
    // alignment 1 and corrupts the heap on Windows.
    device.rdram.mem = AlignedBytes::new_zeroed(device.rdram.size as usize);

    retroachievements::set_rdram(device.rdram.mem.as_ptr(), device.rdram.size as usize);

    // hack, skip RDRAM initialization
    device
        .rdram
        .mem
        .get_mut(0x318..0x318 + 4)
        .unwrap_or(&mut [0; 4])
        .copy_from_slice(&device.rdram.size.to_ne_bytes());
    // hack, skip RDRAM initialization
    device
        .rdram
        .mem
        .get_mut(0x3f0..0x3f0 + 4)
        .unwrap_or(&mut [0; 4])
        .copy_from_slice(&device.rdram.size.to_ne_bytes());

    device.ri.regs[device::ri::RI_MODE_REG] = 0x0e;
    device.ri.regs[device::ri::RI_CONFIG_REG] = 0x40;
}

pub fn rdram_calculate_cycles(length: u64) -> u64 {
    31 + (length / 3) // https://hcs64.com/dma.html, https://github.com/rasky/n64-systembench
}
