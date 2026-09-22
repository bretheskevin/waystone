package dev.waystone.app.nav

import androidx.activity.ComponentActivity
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import androidx.navigation.NavHostController
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import dev.waystone.app.ServiceLocator
import dev.waystone.app.ui.ConflictsScreen
import dev.waystone.app.ui.DashboardScreen
import dev.waystone.app.ui.HistoryScreen
import dev.waystone.app.ui.SetupScreen
import dev.waystone.app.ui.SettingsScreen
import dev.waystone.app.ui.SnapshotsScreen
import dev.waystone.app.ui.UnlockScreen
import dev.waystone.app.vm.ConflictsViewModel
import dev.waystone.app.vm.DashboardViewModel
import dev.waystone.app.vm.HistoryViewModel
import dev.waystone.app.vm.SessionViewModel
import dev.waystone.app.vm.SettingsViewModel
import dev.waystone.app.vm.SetupViewModel
import dev.waystone.app.vm.SnapshotsViewModel
import dev.waystone.app.vm.UnlockViewModel

@Composable
fun WaystoneNavHost(startDestination: String = Routes.SETUP) {
    val navController = rememberNavController()
    val activity = LocalContext.current as ComponentActivity
    val session: SessionViewModel = viewModel(viewModelStoreOwner = activity)

    NavHost(navController = navController, startDestination = startDestination) {
        composable(Routes.SETUP) {
            val vm: SetupViewModel = viewModel(
                factory = viewModelFactory { initializer {
                    SetupViewModel(
                        activity.application,
                        ServiceLocator.settingsProvider,
                        ServiceLocator.sessionVaultStore,
                        ServiceLocator.davFactory,
                        session,
                    )
                    }
                },
            )
            val state by vm.uiState.collectAsState()
            NavEventEffect(vm.navigateTo.value, navController, vm::consumeNavigation)
            SetupScreen(
                state = state,
                onSubmit = vm::submit,
                onRecoveryAcknowledged = vm::acknowledgeRecovery,
            )
        }

        composable(Routes.UNLOCK) {
            val vm: UnlockViewModel = viewModel(
                factory = viewModelFactory { initializer {
                    UnlockViewModel(
                        activity.application,
                        ServiceLocator.settingsProvider,
                        ServiceLocator.sessionVaultStore,
                        ServiceLocator.davProvider,
                        ServiceLocator.davFactory,
                        session,
                    )
                    }
                },
            )
            val state by vm.uiState.collectAsState()
            val passphrase by vm.passphrase.collectAsState()
            val useRecovery by vm.useRecoveryKey.collectAsState()
            val needsWebdav by vm.needsWebdavPassword.collectAsState()
            NavEventEffect(vm.navigateTo.value, navController, vm::consumeNavigation)
            UnlockScreen(
                state = state,
                passphrase = passphrase,
                useRecoveryKey = useRecovery,
                needsWebdavPassword = needsWebdav,
                onToggleRecovery = { vm.useRecoveryKey.value = it },
                onSecretChange = { vm.passphrase.value = it },
                onWebdavPasswordChange = vm::updateWebdavPassword,
                onSubmit = vm::submit,
            )
        }

        composable(Routes.DASHBOARD) {
            val vm: DashboardViewModel = viewModel(
                factory = viewModelFactory { initializer {
                    DashboardViewModel(
                        activity.application,
                        session,
                        ServiceLocator.settingsProvider,
                        ServiceLocator.saveSourceProvider,
                        ServiceLocator.snapshotStoreProvider,
                    )
                    }
                },
            )
            val state by vm.uiState.collectAsState()
            DashboardScreen(
                state = state,
                onRefresh = vm::refresh,
                onSyncAll = vm::syncAll,
                onSyncSource = vm::syncSource,
                onPush = vm::push,
                onPull = vm::pull,
                onOpenHistory = { navController.navigate(Routes.history(it)) },
                onOpenSnapshots = { navController.navigate(Routes.snapshots(it)) },
                onAddSource = vm::addSource,
                onOpenConflicts = { navController.navigate(Routes.CONFLICTS) },
                onOpenSettings = { navController.navigate(Routes.SETTINGS) },
            )
        }

        composable(Routes.CONFLICTS) {
            val vm: ConflictsViewModel = viewModel(
                factory = viewModelFactory { initializer {
                    ConflictsViewModel(
                        activity.application,
                        session,
                        ServiceLocator.settingsProvider,
                        ServiceLocator.saveSourceProvider,
                        ServiceLocator.snapshotStoreProvider,
                    )
                    }
                },
            )
            val state by vm.uiState.collectAsState()
            val conflicts by vm.conflicts.collectAsState()
            NavEventEffect(vm.navigateTo.value, navController, vm::consumeNavigation)
            ConflictsScreen(
                state = state,
                conflicts = conflicts,
                onKeepLocal = vm::keepLocal,
                onKeepRemote = vm::keepRemote,
            )
        }

        composable(
            Routes.HISTORY,
            arguments = listOf(navArgument("groupKey") { type = NavType.StringType }),
        ) { entry ->
            val groupKey = entry.arguments?.getString("groupKey").orEmpty()
            val vm: HistoryViewModel = viewModel(
                factory = viewModelFactory { initializer {
                    HistoryViewModel(
                        activity.application,
                        session,
                        ServiceLocator.settingsProvider,
                        ServiceLocator.saveSourceProvider,
                        ServiceLocator.snapshotStoreProvider,
                        groupKey,
                    )
                    }
                },
            )
            val state by vm.uiState.collectAsState()
            HistoryScreen(state = state, onRestore = vm::restore)
        }

        composable(
            Routes.SNAPSHOTS,
            arguments = listOf(navArgument("groupKey") { type = NavType.StringType }),
        ) { entry ->
            val groupKey = entry.arguments?.getString("groupKey").orEmpty()
            val vm: SnapshotsViewModel = viewModel(
                factory = viewModelFactory { initializer {
                    SnapshotsViewModel(
                        activity.application,
                        session,
                        ServiceLocator.settingsProvider,
                        ServiceLocator.saveSourceProvider,
                        ServiceLocator.snapshotStoreProvider,
                        groupKey,
                    )
                    }
                },
            )
            val state by vm.uiState.collectAsState()
            SnapshotsScreen(state = state, onRestore = vm::restore)
        }

        composable(Routes.SETTINGS) {
            val vm: SettingsViewModel = viewModel(
                factory = viewModelFactory { initializer {
                    SettingsViewModel(
                        activity.application,
                        ServiceLocator.settingsProvider,
                        ServiceLocator.sessionVaultStore,
                        session,
                    )
                    }
                },
            )
            val state by vm.uiState.collectAsState()
            NavEventEffect(vm.navigateTo.value, navController, vm::consumeNavigation)
            SettingsScreen(
                state = state,
                onChangeServer = vm::changeServer,
                onPolicyChange = vm::setConflictPolicy,
                onSafetyBackupChange = vm::setSafetyBackup,
                onLogout = vm::logout,
            )
        }
    }
}

@Composable
private fun NavEventEffect(event: String?, navController: NavHostController, onConsumed: () -> Unit) {
    LaunchedEffect(event) {
        event?.let {
            navController.navigate(it)
            onConsumed()
        }
    }
}
