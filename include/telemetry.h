#pragma once

#ifdef __cplusplus
#include <string>

// =====================================================================
//   Karamelo Anonymous Telemetry & Health Heartbeat
// =====================================================================
// Sends an anonymous, lightweight heartbeat ping once per session at
// startup to monitor version distribution, operating systems, and active
// graphics backends across the user base.
//
// LGPD / GDPR Legitimate Interest:
// - ZERO ROMs or game names
// - ZERO playtime or session durations
// - ZERO user credentials or personal directories
// - Completely opt-out via Config/karamelo.ini [Diagnostics] telemetry=0
// =====================================================================

void TelemetryInit();
void TelemetrySendHeartbeat();
bool TelemetryIsEnabled();
void TelemetrySetEnabled(bool enabled);
void TelemetrySetTransientDisable(bool disable);

#endif // __cplusplus
