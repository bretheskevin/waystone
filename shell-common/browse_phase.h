#ifndef WAYSTONE_BROWSE_PHASE_H
#define WAYSTONE_BROWSE_PHASE_H

// Phase of a browse/scan/restore operation.
// Shared between history and snapshot features on both consoles.
enum class BrowsePhase { Idle, Scanning, Ready, Restoring, Done, Error };

#endif
