/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ODIN_PICTURE_FILTER_VK_HPP
#define ODIN_PICTURE_FILTER_VK_HPP

#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "odin/host/video/picture_preset.hpp"
#include "odin/host/gfx/vk_common.hpp"

namespace odin {

/* A filter of the picture with Vulkan, by the rules picture_filter_gl.hpp
 * tells: the same preset (picture_preset.hpp), each pass from the SPIR-V the
 * build made beside its GLSL (filter_spirv.awk), the parameters of the
 * preset given as its specialization constants; what a pass reads found by
 * name in its SPIR-V. The pictures keep OpenGL's order, the bottom line
 * first; the last pass draws into the window turned over. */
class VkPictureFilter {
public:
	/* The filter of a preset, its passes built; nothing, said on stderr,
	 * when the preset or the SPIR-V of a pass cannot be read */
	static std::unique_ptr<VkPictureFilter> load(vk::Context &card, const std::string &preset);
	~VkPictureFilter();
	VkPictureFilter(const VkPictureFilter &) = delete;
	VkPictureFilter &operator=(const VkPictureFilter &) = delete;

	/* The image to draw the picture of the board into, w by h pixels
	 * from its first line (the bottom one) */
	vk::Image &frame(unsigned w, unsigned h);
	/* That picture through the passes, into the rectangle of the window
	 * from x, y (from the top), w by h; `count` the frames drawn so far
	 * (FrameCount) */
	void draw(vk::Image &window, int x, int y, unsigned w, unsigned h, unsigned count);

private:
	using Scale = preset::Plan::Scale;

	/* A texture a pass reads: its image, its size, the size of the
	 * picture in it, how it is read */
	struct Picture {
		vk::Image image;
		std::array<float, 2> size{}, texture_size{};
		bool linear = true, is_float = false;
		VkSamplerAddressMode wrap = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	};

	/* The uniforms of a stage, in its default block: its binding (none:
	 * UINT32_MAX), its size and the place of each by name */
	struct Block {
		uint32_t binding = UINT32_MAX;
		uint32_t size = 0;
		std::map<std::string, uint32_t> offsets;
	};

	struct Pass {
		VkShaderModule modules[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
		Block blocks[2];				/* vertex, fragment */
		std::map<std::string, uint32_t> textures;	/* binding by name */
		VkDescriptorSetLayout set = VK_NULL_HANDLE;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		std::array<Scale, 2> scale_type{ Scale::SOURCE, Scale::SOURCE };
		std::array<float, 2> scale{ 1.0f, 1.0f };
		bool window = false;	/* the last: into the window */
		Picture out;
		/* Four corners: VertexCoord, then TexCoord, set by size() */
		std::array<float, 16> corners{};
	};

	VkPictureFilter(vk::Context &card) : m_card(card), m_fn(card.fn()) {}
	bool build(const preset::Plan &plan);
	bool build_pass(Pass &pass, const preset::Plan::Pass &from);
	bool lay_out(Pass &pass, const std::string &file);
	void size(unsigned w, unsigned h);
	void make_picture(Picture &picture, unsigned w, unsigned h);
	void clear(Picture &picture);
	VkSampler sampler(bool linear, VkSamplerAddressMode wrap);

	vk::Context &m_card;
	const vk::Functions &m_fn;
	std::vector<Pass> m_passes;
	/* The parameters of the preset, its values */
	std::vector<std::pair<std::string, float>> m_parameters;
	Picture m_frame;
	std::array<unsigned, 2> m_frame_size{}, m_window{};
	/* What a float_framebuffer is drawn in: 32-bit floats as OpenGL's,
	 * 16-bit where the card cannot filter those */
	VkFormat m_float = VK_FORMAT_R32G32B32A32_SFLOAT;
	std::map<std::pair<bool, VkSamplerAddressMode>, VkSampler> m_samplers;
};

} /* namespace odin */

#endif /* ODIN_PICTURE_FILTER_VK_HPP */
