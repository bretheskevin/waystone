#ifndef WAYSTONE_SYNC_H
#define WAYSTONE_SYNC_H

#include "net.h"
#include "saves.h"
#include "sync_engine.h"

// Switch ShellOps for the shared engine. ctx = the AccountUid whose saves are synced; it must
// outlive every engine call made with these ops. `title` = const TitleInfo*.
ShellOps nx_shell_ops(AccountUid* uid);

#endif
