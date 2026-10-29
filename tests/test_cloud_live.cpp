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

    // Silence must be gated by Silero VAD (returns false = do not transmit packet)
    VoiceChatSetDenoiseEngine(VOICE_DENOISE_DEEPFILTERNET);
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

    // Test all 3 selectable denoise engines (DeepFilterNet, RNNoise, Off)
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
    ASSERT_EQ(decoded_pcm[10], (int16_t)(speech_mic[10] & 0xFF00));

    VoiceChatShutdown();
}

TEST_CASE(DesktopChat_WireFormatEphemeralRamAndChallenge)
{
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
    DesktopChatInit();

    // 1. KarameloTag BattleTag format
    std::string tag = DesktopChatGetKarameloTag();
    ASSERT_TRUE(tag.find('#') != std::string::npos);
    DesktopChatSetKarameloTag("GuhPlayer#7777");
    ASSERT_STR_EQ(DesktopChatGetKarameloTag(), "GuhPlayer#7777");

    // 2. Room codes (KRM-XXXX)
    std::string room_code = DesktopChatGenerateRoomCode("192.168.1.100", 55435);
    ASSERT_TRUE(room_code.rfind("KRM-", 0) == 0);
    ASSERT_EQ(room_code.length(), (size_t)8);

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

