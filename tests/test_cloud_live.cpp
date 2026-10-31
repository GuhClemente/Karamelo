#include "test_framework.h"
#include "cloud_sync.h"
#include "web_admin.h"
#include "voice_chat.h"
#include "tray_manager.h"
#include "desktop_chat.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <time.h>
#include "json_mini.h"
#include "system_registry.h"

// -----------------------------------------------------------------------------
// Pilar A: Karamelo Cloud (BYOS Google Drive appDataFolder) Tests
// -----------------------------------------------------------------------------

TEST_CASE(CloudSync_Sha256AndPkceVectors)
{
    // FIPS 180-4 empty string and "abc" vectors
    const uint8_t empty_bytes[1] = { 0 };
    std::string h_empty = CloudSyncComputeSha256Bytes(empty_bytes, 0);
    ASSERT_STR_EQ(h_empty, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    const uint8_t abc[3] = { 'a', 'b', 'c' };
    std::string h_abc = CloudSyncComputeSha256Bytes(abc, 3);
    ASSERT_STR_EQ(h_abc, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // RFC 7636 Appendix B PKCE S256 vector
    std::string verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    std::string challenge = CloudSyncBuildPkceChallenge(verifier);
    ASSERT_STR_EQ(challenge, "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

TEST_CASE(CloudSync_ManifestRoundtripAndConflictResolution)
{
    CloudProfileManifest m;
    m.schema_version = 1;
    m.app_version = "2.5.0";
    m.device_name = "TEST-PC";
    m.updated_utc = 1730000000LL;

    CloudSaveEntry e1;
    e1.rel_path = "saves/SNES/SuperMarioWorld.srm";
    e1.system_dir = "SNES";
    e1.game_stem = "SuperMarioWorld";
    e1.sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    e1.size_bytes = 8192;
    e1.modified_utc = 1730000000LL;
    e1.playtime_sec = 3600;
    m.entries.push_back(e1);

    CloudSaveEntry e2;
    e2.rel_path = "Config/favorites.txt";
    e2.system_dir = "Config";
    e2.game_stem = "favorites";
    e2.sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    e2.size_bytes = 512;
    e2.modified_utc = 1729999000LL;
    e2.playtime_sec = 0;
    m.entries.push_back(e2);

    std::string json_text = CloudSyncSerializeManifest(m);
    ASSERT_TRUE(json_text.find("SuperMarioWorld.srm") != std::string::npos);

    CloudProfileManifest parsed;
    ASSERT_TRUE(CloudSyncParseManifest(json_text, parsed));
    ASSERT_EQ(parsed.schema_version, 1);
    ASSERT_STR_EQ(parsed.device_name, "TEST-PC");
    ASSERT_EQ(parsed.entries.size(), (size_t)2);
    ASSERT_STR_EQ(parsed.entries[0].rel_path, "saves/SNES/SuperMarioWorld.srm");
    ASSERT_EQ(parsed.entries[0].size_bytes, (uint64_t)8192);
    ASSERT_EQ(parsed.entries[0].playtime_sec, (uint64_t)3600);

    // Conflict resolution rules
    CloudSaveEntry local_e = parsed.entries[0];
    CloudSaveEntry remote_same = parsed.entries[0];
    ASSERT_EQ(CloudSyncEvaluateConflict(local_e, remote_same, 0), CLOUD_CONFLICT_NONE);

    CloudSaveEntry remote_newer = local_e;
    remote_newer.sha256 = "0000000000000000000000000000000000000000000000000000000000000001";
    remote_newer.modified_utc = local_e.modified_utc + 120;
    ASSERT_EQ(CloudSyncEvaluateConflict(local_e, remote_newer, local_e.modified_utc), CLOUD_CONFLICT_DOWNLOAD_REMOTE);

    CloudSaveEntry local_newer = local_e;
    local_newer.sha256 = "0000000000000000000000000000000000000000000000000000000000000002";
    local_newer.modified_utc = remote_newer.modified_utc + 300;
    ASSERT_EQ(CloudSyncEvaluateConflict(local_newer, remote_newer, remote_newer.modified_utc), CLOUD_CONFLICT_UPLOAD_LOCAL);

    // Diverged when both modified after last_synced_utc
    ASSERT_EQ(CloudSyncEvaluateConflict(local_newer, remote_newer, local_e.modified_utc - 10), CLOUD_CONFLICT_DIVERGED);
}

TEST_CASE(CloudSync_AllowedSyncPathsFilter)
{
    ASSERT_TRUE(CloudSyncIsAllowedSyncPath("saves/SNES/ChronoTrigger.srm"));
    ASSERT_TRUE(CloudSyncIsAllowedSyncPath("saves/GBA/Mother3.sav"));
    ASSERT_TRUE(CloudSyncIsAllowedSyncPath("saves/PlayStation/Castlevania.mcr"));
    ASSERT_TRUE(CloudSyncIsAllowedSyncPath("Config/favorites.txt"));
    ASSERT_TRUE(CloudSyncIsAllowedSyncPath("Config/recent.txt"));

    // Must never sync heavy ROMs/ISOs, savestates, tokens, or path traversals
    ASSERT_FALSE(CloudSyncIsAllowedSyncPath("saves/PlayStation/Castlevania.state0"));
    ASSERT_FALSE(CloudSyncIsAllowedSyncPath("roms/PlayStation/Castlevania.chd"));
    ASSERT_FALSE(CloudSyncIsAllowedSyncPath("saves/../roms/game.iso"));
    ASSERT_FALSE(CloudSyncIsAllowedSyncPath("Config/cloud_token.dat"));
    ASSERT_FALSE(CloudSyncIsAllowedSyncPath("Config/retroachievements.cfg"));
}

// -----------------------------------------------------------------------------
// Pilar B: Local Web Admin Security & BIOS Auditor Tests
// -----------------------------------------------------------------------------

TEST_CASE(WebAdmin_PathTraversalAndMagicValidation)
{
    ASSERT_STR_EQ(WebAdminSanitizeFilename("../../etc/passwd"), "");
    ASSERT_STR_EQ(WebAdminSanitizeFilename("..\\..\\Windows\\win.ini"), "");
    ASSERT_STR_EQ(WebAdminSanitizeFilename(".hidden_file"), "");
    ASSERT_STR_EQ(WebAdminSanitizeFilename("scph5501.bin"), "scph5501.bin");
    ASSERT_STR_EQ(WebAdminSanitizeFilename("Super Mario World (USA).sfc"), "Super Mario World (USA).sfc");

    ASSERT_TRUE(WebAdminIsAllowedUploadExtension("scph5501.bin"));
    ASSERT_TRUE(WebAdminIsAllowedUploadExtension("metal_slug.zip"));
    ASSERT_TRUE(WebAdminIsAllowedUploadExtension("castlevania.chd"));
    ASSERT_FALSE(WebAdminIsAllowedUploadExtension("malware.exe"));
    ASSERT_FALSE(WebAdminIsAllowedUploadExtension("script.bat"));
    ASSERT_FALSE(WebAdminIsAllowedUploadExtension("shell.sh"));
    ASSERT_FALSE(WebAdminIsAllowedUploadExtension("payload.dll"));

    const uint8_t zip_hdr[8] = { 'P', 'K', 0x03, 0x04, 0x00, 0x00, 0x00, 0x00 };
    const uint8_t bad_zip[8] = { 'M', 'Z', 0x90, 0x00, 0x03, 0x00, 0x00, 0x00 };
    ASSERT_TRUE(WebAdminValidateMagicAndContent("game.zip", zip_hdr, sizeof(zip_hdr)));
    ASSERT_FALSE(WebAdminValidateMagicAndContent("game.zip", bad_zip, sizeof(bad_zip)));

    const uint8_t chd_hdr[8] = { 'M', 'C', 'o', 'm', 'p', 'r', 'H', 'D' };
    ASSERT_TRUE(WebAdminValidateMagicAndContent("disc.chd", chd_hdr, sizeof(chd_hdr)));
    ASSERT_FALSE(WebAdminValidateMagicAndContent("disc.chd", zip_hdr, sizeof(zip_hdr)));
}

TEST_CASE(WebAdmin_BiosAuditorAndLoopbackHttpSecurity)
{
    WebAdminInit();
    std::string audit_json = WebAdminBuildBiosAuditJson();
    ASSERT_TRUE(audit_json.find("\"items\":[") != std::string::npos);
    ASSERT_TRUE(audit_json.find("PlayStation 2") != std::string::npos);
    ASSERT_TRUE(audit_json.find("Sega Saturn") != std::string::npos);

    // Start WebAdmin bound strictly to 127.0.0.1 for unit test verification
    ASSERT_TRUE(WebAdminStart(18089, true));
    ASSERT_TRUE(WebAdminIsRunning());

    auto http_req = [](const std::string& req) -> std::string {
        int s = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s < 0) return "";
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(18089);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
#ifdef _WIN32
            closesocket(s);
#else
            close(s);
#endif
            return "";
        }
        send(s, req.data(), (int)req.size(), 0);
        std::string resp;
        char buf[2048];
        for (;;) {
            int n = recv(s, buf, sizeof(buf), 0);
            if (n <= 0) break;
            resp.append(buf, (size_t)n);
        }
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return resp;
    };

    // 1. Check CSP and security headers on GET /
    std::string get_root = http_req("GET / HTTP/1.1\r\nHost: 127.0.0.1:18089\r\nConnection: close\r\n\r\n");
    ASSERT_TRUE(get_root.find("HTTP/1.1 200 OK") != std::string::npos);
    ASSERT_TRUE(get_root.find("Content-Security-Policy: default-src 'self'") != std::string::npos);
    ASSERT_TRUE(get_root.find("X-Frame-Options: DENY") != std::string::npos);

    // 2. Unauthenticated GET /api/bios-audit must return 401
    std::string unauth = http_req("GET /api/bios-audit HTTP/1.1\r\nHost: 127.0.0.1:18089\r\nConnection: close\r\n\r\n");
    ASSERT_TRUE(unauth.find("HTTP/1.1 401") != std::string::npos);

    // 3. Authenticate with generated 4-digit PIN
    std::string pin = WebAdminGetPin();
    ASSERT_EQ(pin.size(), (size_t)4);
    std::string login_body = "{\"pin\":\"" + pin + "\"}";
    std::string login_req =
        "POST /api/login HTTP/1.1\r\n"
        "Host: 127.0.0.1:18089\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(login_body.size()) + "\r\n"
        "Connection: close\r\n\r\n" + login_body;
    std::string login_resp = http_req(login_req);
    ASSERT_TRUE(login_resp.find("HTTP/1.1 200 OK") != std::string::npos);
    ASSERT_TRUE(login_resp.find("HttpOnly; SameSite=Strict") != std::string::npos);
    ASSERT_TRUE(login_resp.find("\"csrf_token\":\"") != std::string::npos);

    // Extract session cookie
    size_t ck_pos = login_resp.find("karamelo_sid=");
    ASSERT_TRUE(ck_pos != std::string::npos);
    size_t ck_end = login_resp.find(';', ck_pos);
    std::string sid_cookie = login_resp.substr(ck_pos, ck_end - ck_pos);

    // 4. POST /api/upload without X-CSRF-Token must be rejected with 403 Forbidden
    std::string no_csrf_req =
        "POST /api/upload?target=bios&filename=scph5501.bin HTTP/1.1\r\n"
        "Host: 127.0.0.1:18089\r\n"
        "Cookie: " + sid_cookie + "\r\n"
        "Content-Length: 4\r\n"
        "Connection: close\r\n\r\nTEST";
    std::string no_csrf_resp = http_req(no_csrf_req);
    ASSERT_TRUE(no_csrf_resp.find("HTTP/1.1 403") != std::string::npos);

    // 5. Test RetroAchievements & Emulation APIs via Web Studio
    std::string ra_req =
        "GET /api/ra/status HTTP/1.1\r\n"
        "Host: 127.0.0.1:18089\r\n"
        "Cookie: " + sid_cookie + "\r\n"
        "Connection: close\r\n\r\n";
    std::string ra_resp = http_req(ra_req);
    ASSERT_TRUE(ra_resp.find("HTTP/1.1 200 OK") != std::string::npos);
    ASSERT_TRUE(ra_resp.find("\"username\":") != std::string::npos);
    // The saved password or token never goes over the network, only the fact
    // that one exists.
    ASSERT_TRUE(ra_resp.find("\"has_token\":") != std::string::npos);
    ASSERT_TRUE(ra_resp.find("\"token\":") == std::string::npos);

    std::string ach_req =
        "GET /api/ra/achievements HTTP/1.1\r\n"
        "Host: 127.0.0.1:18089\r\n"
        "Cookie: " + sid_cookie + "\r\n"
        "Connection: close\r\n\r\n";
    std::string ach_resp = http_req(ach_req);
    ASSERT_TRUE(ach_resp.find("HTTP/1.1 200 OK") != std::string::npos);
    ASSERT_TRUE(ach_resp.find("\"achievements\":") != std::string::npos);

    std::string emu_req =
        "GET /api/emulation/config HTTP/1.1\r\n"
        "Host: 127.0.0.1:18089\r\n"
        "Cookie: " + sid_cookie + "\r\n"
        "Connection: close\r\n\r\n";
    std::string emu_resp = http_req(emu_req);
    ASSERT_TRUE(emu_resp.find("HTTP/1.1 200 OK") != std::string::npos);
    ASSERT_TRUE(emu_resp.find("\"aspect\":") != std::string::npos);

    // 6. Test Systems Visibility APIs
    std::string sys_vis_req =
        "GET /api/systems/visibility HTTP/1.1\r\n"
        "Host: 127.0.0.1:18089\r\n"
        "Cookie: " + sid_cookie + "\r\n"
        "Connection: close\r\n\r\n";
    std::string sys_vis_resp = http_req(sys_vis_req);
    ASSERT_TRUE(sys_vis_resp.find("HTTP/1.1 200 OK") != std::string::npos);
    ASSERT_TRUE(sys_vis_resp.find("\"total\":38") != std::string::npos);
    ASSERT_TRUE(sys_vis_resp.find("\"visible_count\":") != std::string::npos);
    ASSERT_TRUE(sys_vis_resp.find("Super Nintendo") != std::string::npos);

    size_t csrf_pos = login_resp.find("\"csrf_token\":\"");
    ASSERT_TRUE(csrf_pos != std::string::npos);
    size_t csrf_start = csrf_pos + 14;
    size_t csrf_end = login_resp.find('"', csrf_start);
    std::string csrf_tok = login_resp.substr(csrf_start, csrf_end - csrf_start);

    std::string post_vis_req =
        "POST /api/systems/visibility HTTP/1.1\r\n"
        "Host: 127.0.0.1:18089\r\n"
        "Cookie: " + sid_cookie + "\r\n"
        "X-CSRF-Token: " + csrf_tok + "\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 21\r\n"
        "Connection: close\r\n\r\n"
        "{\"action\":\"show_all\"}";
    std::string post_vis_resp = http_req(post_vis_req);
    ASSERT_TRUE(post_vis_resp.find("HTTP/1.1 200 OK") != std::string::npos);

    WebAdminStop();
    ASSERT_FALSE(WebAdminIsRunning());
}

// -----------------------------------------------------------------------------
// Pilar C, D & E: Voice Chat DSP, Tray & Ephemeral Live Chat Tests
// -----------------------------------------------------------------------------

TEST_CASE(VoiceChat_ModularDspAndPacketFraming)
{
    VoiceChatInit();
    VoiceChatSetEnabled(true);
    VoiceChatSetAecEnabled(true);
    VoiceChatSetAgcEnabled(true);
    VoiceChatSetVadEnabled(true);
    VoiceChatSetVadSensitivity(VOICE_VAD_MEDIUM);

    const size_t kFrameSamples = 320; // 20ms @ 16kHz
    int16_t silence_mic[kFrameSamples] = { 0 };
    int16_t ref_echo[kFrameSamples] = { 0 };

    // Silence must be gated by the voice detector (returns false = do not transmit packet)
    VoiceChatSetDenoiseEngine(VOICE_DENOISE_STRONG);
    VoiceChatConfig cfg = VoiceChatGetConfig();
    float vad_prob = 0.0f;
    bool vad_silence = VoiceChatProcessFrame(silence_mic, kFrameSamples, ref_echo, cfg, &vad_prob);
    ASSERT_FALSE(vad_silence);

    // Simulated speech signal (400 Hz + 800 Hz harmonic above VAD threshold)
    int16_t speech_mic[kFrameSamples];
    for (size_t i = 0; i < kFrameSamples; i++) {
        double t = (double)i / 16000.0;
        speech_mic[i] = (int16_t)(4500.0 * std::sin(2.0 * 3.14159265 * 400.0 * t) +
                                  2500.0 * std::sin(2.0 * 3.14159265 * 800.0 * t));
    }

    // Test all 3 selectable denoise engines (strong, light, off)
    for (int eng = 0; eng <= 2; eng++) {
        VoiceChatSetDenoiseEngine((VoiceDenoiseEngine)eng);
        cfg = VoiceChatGetConfig();
        int16_t frame_buf[kFrameSamples];
        std::memcpy(frame_buf, speech_mic, sizeof(speech_mic));
        bool active = VoiceChatProcessFrame(frame_buf, kFrameSamples, ref_echo, cfg, &vad_prob);
        ASSERT_TRUE(active);
    }

    // Test KVC1 UDP packet framing
    std::vector<uint8_t> pkt;
    ASSERT_TRUE(VoiceChatBuildPacket(42, true, speech_mic, kFrameSamples, pkt));
    ASSERT_TRUE(pkt.size() > 10);

    uint32_t parsed_seq = 0;
    bool parsed_vad = false;
    std::vector<int16_t> decoded_pcm;
    ASSERT_TRUE(VoiceChatParsePacket(pkt.data(), pkt.size(), &parsed_seq, &parsed_vad, decoded_pcm));
    ASSERT_EQ(parsed_seq, (uint32_t)42);
    ASSERT_TRUE(parsed_vad);
    ASSERT_EQ(decoded_pcm.size(), kFrameSamples);
    ASSERT_TRUE(std::abs((int)decoded_pcm[10] - (int)speech_mic[10]) <= std::abs((int)speech_mic[10]) / 16 + 64);

    VoiceChatShutdown();
}

TEST_CASE(DesktopChat_WireFormatEphemeralRamAndChallenge)
{
    DesktopChatSetNetworkAllowed(false);
    DesktopChatInit();
    DesktopChatSetNickname("GuhPlayer");
    DesktopChatSetRoom(LIVE_ROOM_ARCADE_FIGHT);

    LiveChatMessage msg;
    msg.sender = "GuhPlayer";
    msg.text = "Quem vem KOF 98?";
    msg.game_challenge = "The King of Fighters '98";
    msg.host_ip = "192.168.1.50";
    msg.host_port = 55435;
    msg.timestamp_utc = 1730000123LL;

    std::string wire = DesktopChatFormatWireMessage(LIVE_ROOM_ARCADE_FIGHT, msg);
    ASSERT_TRUE(wire.find("\"sender\":\"GuhPlayer\"") != std::string::npos);

    LiveChatRoom parsed_room = LIVE_ROOM_LOUNGE;
    LiveChatMessage parsed;
    ASSERT_TRUE(DesktopChatParseWireMessage(wire, &parsed_room, parsed));
    ASSERT_EQ(parsed_room, LIVE_ROOM_ARCADE_FIGHT);
    ASSERT_STR_EQ(parsed.sender, "GuhPlayer");
    ASSERT_STR_EQ(parsed.text, "Quem vem KOF 98?");
    ASSERT_STR_EQ(parsed.game_challenge, "The King of Fighters '98");
    ASSERT_STR_EQ(parsed.host_ip, "192.168.1.50");
    ASSERT_EQ(parsed.host_port, 55435);

    ASSERT_TRUE(DesktopChatPostMessage("Salve galera do Arcade!"));
    ASSERT_TRUE(DesktopChatSendChallenge("Street Fighter III: 3rd Strike"));
    ASSERT_TRUE(DesktopChatGetRoomMessages().size() >= 2);
    ASSERT_TRUE(DesktopChatAcceptLastChallenge());

    // Verify In-Game Overlay renders cleanly onto a 640x360 canvas
    std::vector<uint32_t> canvas(640 * 360, 0);
    DesktopChatSetOverlayVisible(true);
    DesktopChatRenderOverlay(canvas.data(), 640, 360);
    bool any_pixel_drawn = false;
    for (uint32_t px : canvas) {
        if (px != 0) { any_pixel_drawn = true; break; }
    }
    ASSERT_TRUE(any_pixel_drawn);

    // Ephemeral RAM wipe on shutdown
    DesktopChatShutdown();
    ASSERT_EQ(DesktopChatGetRoomMessages().size(), (size_t)0);
}

TEST_CASE(DesktopChat_FriendsListAndKarameloTagPresence)
{
    DesktopChatSetNetworkAllowed(false);
    DesktopChatInit();

    // 1. KarameloTag BattleTag format
    std::string tag = DesktopChatGetKarameloTag();
    ASSERT_TRUE(tag.find('#') != std::string::npos);
    DesktopChatSetKarameloTag("GuhPlayer#7777");
    ASSERT_STR_EQ(DesktopChatGetKarameloTag(), "GuhPlayer#7777");

    // 2. Room codes (KRM-XXXX)
    std::string room_code = DesktopChatGenerateRoomCode("192.168.1.100", 55435);
    ASSERT_TRUE(room_code.rfind("KRM-", 0) == 0);
    ASSERT_EQ(room_code.length(), (size_t)15);

    std::string resolved_ip;
    int resolved_port = 0;
    ASSERT_TRUE(DesktopChatResolveRoomCode(room_code, resolved_ip, resolved_port));
    ASSERT_STR_EQ(resolved_ip, "192.168.1.100");
    ASSERT_EQ(resolved_port, 55435);

    // 3. Persistent Friends list & favorites
    ASSERT_TRUE(DesktopChatAddFriend("RetroChamp#1234", "RetroChamp"));
    ASSERT_TRUE(DesktopChatIsFriend("RetroChamp#1234"));
    ASSERT_TRUE(DesktopChatToggleFriendFavorite("RetroChamp#1234"));

    auto friends = DesktopChatGetFriends();
    ASSERT_TRUE(friends.size() >= 1);
    bool found_friend = false;
    for (const auto& fr : friends) {
        if (fr.tag == "RetroChamp#1234") {
            found_friend = true;
            ASSERT_TRUE(fr.favorite);
            break;
        }
    }
    ASSERT_TRUE(found_friend);

    // 4. Peer discovery and presence syncing to friend status
    LobbyPresencePeer peer;
    peer.tag = "RetroChamp#1234";
    peer.host_ip = "192.168.1.120";
    peer.host_port = 55435;
    peer.current_game = "Street Fighter Alpha 3";
    peer.status = 2;
    peer.last_seen_utc = (int64_t)time(NULL);
    DesktopChatRegisterPeerPresence(peer);

    auto peers = DesktopChatGetActivePeers();
    ASSERT_TRUE(peers.size() >= 1);

    // Presence automatically updates friend online and active game info
    friends = DesktopChatGetFriends();
    for (const auto& fr : friends) {
        if (fr.tag == "RetroChamp#1234") {
            ASSERT_TRUE(fr.online);
            ASSERT_STR_EQ(fr.current_game, "Street Fighter Alpha 3");
            break;
        }
    }

    // 5. Clean friend removal
    ASSERT_TRUE(DesktopChatRemoveFriend("RetroChamp#1234"));
    ASSERT_FALSE(DesktopChatIsFriend("RetroChamp#1234"));

    DesktopChatShutdown();
}

// -----------------------------------------------------------------------------
// Voice on the wire
// -----------------------------------------------------------------------------

TEST_CASE(VoiceChat_MuLawKeepsQuietSpeech)
{
    // Keeping only the high byte turned everything under 256 into silence.
    const size_t kCount = 320;
    std::vector<int16_t> pcm(kCount);
    double signal_energy = 0.0;
    for (size_t i = 0; i < kCount; i++) {
        pcm[i] = (int16_t)(200.0 * std::sin(2.0 * 3.14159265 * 300.0 * (double)i / 16000.0));
        signal_energy += (double)pcm[i] * (double)pcm[i];
    }

    std::vector<uint8_t> packet;
    ASSERT_TRUE(VoiceChatBuildPacket(77, true, pcm.data(), kCount, packet));
    ASSERT_EQ(packet.size(), (size_t)(11 + kCount));

    uint32_t seq = 0;
    bool vad = false;
    std::vector<int16_t> out;
    ASSERT_TRUE(VoiceChatParsePacket(packet.data(), packet.size(), &seq, &vad, out));
    ASSERT_EQ(seq, (uint32_t)77);
    ASSERT_TRUE(vad);
    ASSERT_EQ(out.size(), kCount);

    double noise_energy = 0.0;
    for (size_t i = 0; i < kCount; i++) {
        double d = (double)pcm[i] - (double)out[i];
        noise_energy += d * d;
    }
    // At least 20 dB of signal over quantization noise.
    ASSERT_TRUE(signal_energy > noise_energy * 100.0);

    packet[0] = 'X';
    ASSERT_FALSE(VoiceChatParsePacket(packet.data(), packet.size(), &seq, &vad, out));
}

TEST_CASE(VoiceChat_OffByDefaultAndFollowsTheMatch)
{
    VoiceChatConfig fresh;
    ASSERT_FALSE(fresh.enabled); // opening the microphone is the player's decision

    VoiceChatSetEnabled(false);
    VoiceChatSyncWithNetplay(true, "127.0.0.1");
    ASSERT_FALSE(VoiceChatIsActive());
    VoiceChatSyncWithNetplay(false, "");
    ASSERT_FALSE(VoiceChatIsActive());
}

// -----------------------------------------------------------------------------
// Chat over the network
// -----------------------------------------------------------------------------

TEST_CASE(DesktopChat_RoomCodeWorksOnAnotherMachine)
{
    DesktopChatSetNetworkAllowed(false);
    // The code alone must carry the address: nothing is registered anywhere.
    std::string code = DesktopChatGenerateRoomCode("203.0.113.77", 55435);
    ASSERT_EQ(code.size(), (size_t)15);
    ASSERT_TRUE(code.rfind("KRM-", 0) == 0);

    std::string ip;
    int port = 0;
    ASSERT_TRUE(DesktopChatResolveRoomCode(code, ip, port));
    ASSERT_STR_EQ(ip, "203.0.113.77");
    ASSERT_EQ(port, 55435);

    // Typed by hand: lower case, without dashes.
    std::string typed;
    for (char c : code)
        if (c != '-') typed += (char)tolower((unsigned char)c);
    ip.clear();
    ASSERT_TRUE(DesktopChatResolveRoomCode(typed, ip, port));
    ASSERT_STR_EQ(ip, "203.0.113.77");

    // One wrong letter must not connect somewhere else.
    std::string wrong = code;
    wrong[wrong.size() - 1] = (wrong[wrong.size() - 1] == '2') ? '3' : '2';
    ASSERT_FALSE(DesktopChatResolveRoomCode(wrong, ip, port));
    ASSERT_FALSE(DesktopChatResolveRoomCode("KRM-1234", ip, port));
    ASSERT_STR_EQ(DesktopChatGenerateRoomCode("not-an-ip", 55435), "");
}

TEST_CASE(DesktopChat_ReceivesMessagesAndPresenceFromTheNetwork)
{
    DesktopChatSetNetworkAllowed(false);
    DesktopChatInit();
    DesktopChatSetRoom(LIVE_ROOM_LOUNGE);
    DesktopChatClearRoomMessages();
    size_t before = DesktopChatGetRoomMessages().size();

    LiveChatMessage msg;
    msg.sender = "Amigo";
    msg.text = "bora jogar";
    msg.game_challenge = "Tatsujin";
    msg.host_ip = "10.9.9.9"; // what the packet claims
    msg.host_port = 55435;
    std::string wire = "KRMC1 deadbeef m " + DesktopChatFormatWireMessage(LIVE_ROOM_LOUNGE, msg);
    ASSERT_TRUE(DesktopChatHandleDatagram(wire, "192.168.0.20"));

    auto msgs = DesktopChatGetRoomMessages();
    ASSERT_EQ(msgs.size(), before + 1);
    ASSERT_STR_EQ(msgs.back().sender, "Amigo");
    // An invite connects to whoever sent it, whatever the text says.
    ASSERT_STR_EQ(msgs.back().host_ip, "192.168.0.20");
    std::string ip;
    int port = 0;
    ASSERT_TRUE(DesktopChatResolveRoomCode(msgs.back().room_code, ip, port));
    ASSERT_STR_EQ(ip, "192.168.0.20");

    std::string presence = "KRMC1 deadbeef p {\"tag\":\"Amigo#4242\",\"game\":\"Tatsujin\",\"port\":55435,\"status\":2,\"challenge\":false}";
    ASSERT_TRUE(DesktopChatHandleDatagram(presence, "192.168.0.21"));
    bool seen = false;
    for (const auto& p : DesktopChatGetActivePeers()) {
        if (p.tag == "Amigo#4242") {
            seen = true;
            ASSERT_STR_EQ(p.host_ip, "192.168.0.21");
            ASSERT_STR_EQ(p.current_game, "Tatsujin");
        }
    }
    ASSERT_TRUE(seen);

    // Garbage and floods from the network.
    ASSERT_FALSE(DesktopChatHandleDatagram("oi", "192.168.0.22"));
    ASSERT_FALSE(DesktopChatHandleDatagram("KRMC1 deadbeef m {nao e json", "192.168.0.22"));
    int accepted = 0;
    for (int i = 0; i < 60; i++)
        if (DesktopChatHandleDatagram(wire, "192.168.0.30")) accepted++;
    ASSERT_TRUE(accepted <= 20);

    DesktopChatShutdown();
}

// -----------------------------------------------------------------------------
// Cloud sync against a local stand-in for the Google Drive API
// -----------------------------------------------------------------------------

namespace {

struct MockDriveFile {
    std::string id, name, sha256, mtime, content;
};

struct MockDrive {
    std::atomic<bool> running{false};
    std::thread thread;
    int port = 0;
    int listen_sock = -1;
    std::mutex mtx;
    std::map<std::string, MockDriveFile> files; // by id
    int next_id = 1;

    static void CloseSock(int s) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
    }

    static std::string Header(const std::string& headers, const std::string& name) {
        std::string lower = headers, key = name + ":";
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)tolower(c); });
        size_t p = lower.find(key);
        if (p == std::string::npos) return "";
        p += key.size();
        size_t e = headers.find("\r\n", p);
        std::string v = headers.substr(p, e - p);
        while (!v.empty() && v[0] == ' ') v.erase(0, 1);
        return v;
    }

    void ApplyUpload(const std::string& headers, const std::string& body, MockDriveFile& f) {
        std::string ctype = Header(headers, "Content-Type");
        std::string boundary = ctype.substr(ctype.find("boundary=") + 9);
        std::string sep = "--" + boundary;
        size_t p1 = body.find(sep);
        size_t meta_at = body.find("\r\n\r\n", p1) + 4;
        size_t p2 = body.find(sep, meta_at);
        std::string meta = body.substr(meta_at, p2 - meta_at - 2);
        size_t data_at = body.find("\r\n\r\n", p2) + 4;
        size_t p3 = body.find("\r\n" + sep + "--", data_at);
        f.content = body.substr(data_at, p3 - data_at);

        JsonValue root;
        std::string err;
        if (JsonParse(meta, root, err) && root.type == JsonValue::Object) {
            if (const JsonValue* n = root.Find("name")) f.name = n->str;
            if (const JsonValue* props = root.Find("appProperties")) {
                if (const JsonValue* v = props->Find("sha256")) f.sha256 = v->str;
                if (const JsonValue* v = props->Find("mtime")) f.mtime = v->str;
            }
        }
    }

    void Serve(int cli) {
        std::string req;
        char buf[8192];
        size_t head_end = std::string::npos, need = 0;
        for (;;) {
            int n = recv(cli, buf, sizeof(buf), 0);
            if (n <= 0) break;
            req.append(buf, (size_t)n);
            if (head_end == std::string::npos) {
                head_end = req.find("\r\n\r\n");
                if (head_end != std::string::npos)
                    need = (size_t)atoll(Header(req.substr(0, head_end + 2), "Content-Length").c_str());
            }
            if (head_end != std::string::npos && req.size() >= head_end + 4 + need) break;
        }
        if (head_end == std::string::npos) return;
        std::string headers = req.substr(0, head_end + 2);
        std::string body = req.substr(head_end + 4);
        std::istringstream line(headers);
        std::string method, target;
        line >> method >> target;

        int status = 200;
        std::string out = "{}";
        std::lock_guard<std::mutex> lk(mtx);
        if (target == "/token") {
            if (body.find("refresh_token=good") != std::string::npos) out = "{\"access_token\":\"AT\"}";
            else { status = 400; out = "{\"error\":\"invalid_grant\"}"; }
        } else if (Header(headers, "Authorization") != "Bearer AT") {
            status = 401;
        } else if (method == "GET" && target.rfind("/drive/v3/files?", 0) == 0) {
            std::ostringstream ss;
            ss << "{\"files\":[";
            bool first = true;
            for (const auto& kv : files) {
                if (!first) ss << ",";
                first = false;
                ss << "{\"id\":" << JsonQuote(kv.second.id) << ",\"name\":" << JsonQuote(kv.second.name)
                   << ",\"appProperties\":{\"sha256\":" << JsonQuote(kv.second.sha256)
                   << ",\"mtime\":" << JsonQuote(kv.second.mtime) << "}}";
            }
            ss << "]}";
            out = ss.str();
        } else if (method == "GET" && target.find("?alt=media") != std::string::npos) {
            std::string id = target.substr(16, target.find('?') - 16);
            auto it = files.find(id);
            if (it == files.end()) status = 404;
            else out = it->second.content;
        } else if (method == "POST" && target.rfind("/upload/drive/v3/files?", 0) == 0) {
            MockDriveFile f;
            f.id = "f" + std::to_string(next_id++);
            ApplyUpload(headers, body, f);
            files[f.id] = f;
        } else if (method == "PATCH" && target.rfind("/upload/drive/v3/files/", 0) == 0) {
            std::string id = target.substr(23, target.find('?') - 23);
            auto it = files.find(id);
            if (it == files.end()) status = 404;
            else ApplyUpload(headers, body, it->second);
        } else {
            status = 404;
        }

        std::string resp = "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Length: " +
                           std::to_string(out.size()) + "\r\nConnection: close\r\n\r\n" + out;
        send(cli, resp.data(), (int)resp.size(), 0);
    }

    bool Start() {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        listen_sock = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(listen_sock, 16) != 0) return false;
        socklen_t len = sizeof(addr);
        getsockname(listen_sock, (sockaddr*)&addr, &len);
        port = ntohs(addr.sin_port);
        running.store(true);
        thread = std::thread([this]() {
            while (running.load()) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(listen_sock, &fds);
                timeval tv{0, 100000};
                if (select(listen_sock + 1, &fds, NULL, NULL, &tv) <= 0) continue;
                int cli = (int)accept(listen_sock, NULL, NULL);
                if (cli < 0) continue;
                Serve(cli);
                CloseSock(cli);
            }
        });
        return true;
    }

    void Stop() {
        running.store(false);
        if (thread.joinable()) thread.join();
        CloseSock(listen_sock);
    }

    MockDriveFile* ByName(const std::string& name) {
        for (auto& kv : files)
            if (kv.second.name == name) return &kv.second;
        return nullptr;
    }
};

std::string ReadAll(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void WriteAll(const std::filesystem::path& p, const std::string& s) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}

std::string Sha(const std::string& s) {
    return CloudSyncComputeSha256Bytes((const uint8_t*)s.data(), s.size());
}

} // namespace

TEST_CASE(CloudSync_RealSyncAgainstDriveApi)
{
    namespace fs = std::filesystem;
    MockDrive drive;
    ASSERT_TRUE(drive.Start());
    const std::string base = "http://127.0.0.1:" + std::to_string(drive.port);
    const std::string token_url = base + "/token";

    std::error_code ec;
    fs::path root = fs::temp_directory_path(ec) / "karamelo_cloud_test";
    fs::remove_all(root, ec);
    fs::path save = root / "saves" / "SNES" / "Chrono.srm";
    WriteAll(save, "save-v1");

    CloudSyncReport rep;
    std::string err;

    // Not logged in, or login no longer valid: never reported as synced.
    CloudSyncSetEndpointsForTest(token_url, base, "", 0);
    ASSERT_FALSE(CloudSyncRunNow(root.string(), &rep, &err));
    ASSERT_STR_EQ(err, "sem login");
    CloudSyncSetEndpointsForTest(token_url, base, "revogado", 0);
    ASSERT_FALSE(CloudSyncRunNow(root.string(), &rep, &err));
    ASSERT_STR_EQ(err, "login expirado");
    ASSERT_TRUE(drive.files.empty());

    // First sync: the save really goes up, with its hash.
    CloudSyncSetEndpointsForTest(token_url, base, "good", 0);
    ASSERT_TRUE(CloudSyncRunNow(root.string(), &rep, &err));
    ASSERT_EQ(rep.uploaded, 1);
    MockDriveFile* up = drive.ByName("saves/SNES/Chrono.srm");
    ASSERT_TRUE(up != nullptr);
    ASSERT_STR_EQ(up->content, "save-v1");
    ASSERT_STR_EQ(up->sha256, Sha("save-v1"));

    // Nothing changed: nothing moves.
    ASSERT_TRUE(CloudSyncRunNow(root.string(), &rep, &err));
    ASSERT_EQ(rep.uploaded, 0);
    ASSERT_EQ(rep.downloaded, 0);

    // Newer on the other machine: comes down, old one kept as .bak.
    const int64_t now = (int64_t)time(NULL);
    up->content = "save-v2";
    up->sha256 = Sha("save-v2");
    up->mtime = std::to_string(now + 1000);
    ASSERT_TRUE(CloudSyncRunNow(root.string(), &rep, &err));
    ASSERT_EQ(rep.downloaded, 1);
    ASSERT_STR_EQ(ReadAll(save), "save-v2");
    ASSERT_STR_EQ(ReadAll(fs::path(save.string() + ".bak")), "save-v1");

    // A new machine gets saves it did not have.
    MockDriveFile other;
    other.id = "other";
    other.name = "saves/GBA/Mother3.sav";
    other.content = "gba-save";
    other.sha256 = Sha("gba-save");
    other.mtime = std::to_string(now);
    drive.files[other.id] = other;

    // A name from the network must not choose where a file lands.
    MockDriveFile evil = other;
    evil.id = "evil";
    evil.name = "../../fora.txt";
    drive.files[evil.id] = evil;
    MockDriveFile evil2 = other;
    evil2.id = "evil2";
    evil2.name = "Config/karamelo.cfg";
    drive.files[evil2.id] = evil2;

    // A corrupted download must not replace the save.
    up->content = "corrompido";
    up->sha256 = Sha("o que deveria vir");
    up->mtime = std::to_string(now + 2000);
    ASSERT_FALSE(CloudSyncRunNow(root.string(), &rep, &err));
    ASSERT_STR_EQ(ReadAll(save), "save-v2");

    up->content = "save-v3";
    up->sha256 = Sha("save-v3");
    ASSERT_TRUE(CloudSyncRunNow(root.string(), &rep, &err));
    ASSERT_EQ(rep.downloaded, 2);
    ASSERT_STR_EQ(ReadAll(root / "saves" / "GBA" / "Mother3.sav"), "gba-save");
    ASSERT_FALSE(fs::exists(root.parent_path() / "fora.txt", ec));
    ASSERT_FALSE(fs::exists(root / "Config" / "karamelo.cfg", ec));

    // Changed on both sides since the last sync: nobody's progress is lost.
    WriteAll(save, "local-diferente");
    up->content = "remoto-diferente";
    up->sha256 = Sha("remoto-diferente");
    up->mtime = std::to_string(now + 3000);
    CloudSyncSetEndpointsForTest(token_url, base, "good", now - 100);
    ASSERT_TRUE(CloudSyncRunNow(root.string(), &rep, &err));
    ASSERT_EQ(rep.conflicts, 1);
    ASSERT_STR_EQ(ReadAll(save), "local-diferente");
    ASSERT_STR_EQ(ReadAll(fs::path(save.string() + ".conflito")), "remoto-diferente");
    ASSERT_STR_EQ(up->content, "local-diferente");

    drive.Stop();
    fs::remove_all(root, ec);
    CloudSyncSetEndpointsForTest("https://oauth2.googleapis.com/token", "https://www.googleapis.com", "", 0);
}

// F10 opens the chat overlay, but on a computer the key belongs to the game.
TEST_CASE(DesktopChat_F10StaysWithComputerGames)
{
    ASSERT_TRUE(SystemIsKeyboardComputer("DOS"));
    ASSERT_TRUE(SystemIsKeyboardComputer("dos"));
    ASSERT_TRUE(SystemIsKeyboardComputer("FMTowns"));
    ASSERT_TRUE(SystemIsKeyboardComputer("PC98"));
    ASSERT_TRUE(SystemIsKeyboardComputer("Amiga"));
    ASSERT_TRUE(!SystemIsKeyboardComputer("SNES"));
    ASSERT_TRUE(!SystemIsKeyboardComputer("Arcade"));
    ASSERT_TRUE(!SystemIsKeyboardComputer(""));
}
