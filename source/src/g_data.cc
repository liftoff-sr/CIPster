
#include <trace.h>
#include "cip/cipcommon.h"
#include "cip/cipconnection.h"
#include "enet_encap/encap.h"

int g_CIPSTER_TRACE_LEVEL = CIPSTER_TRACE_LEVEL;

int g_my_io_udp_port = kEIP_IoUdpPort;
//int g_my_io_udp_port = 2200;


/// If this is changed from kEIP_Reserved_Port, then there will be another
/// set of TCP and UDP ports for Encapsulation protocol, but TCP and UDP listeners
/// established, while still preserving the two on kEIP_Reserved_Port.
// int g_my_enip_port = kEIP_Reserved_Port;  not yet


uint32_t g_run_idle_state;
