package dev.waystone.app.nav

/** One-shot navigation request; [clearBackStack] drops every back-stack entry so Back cannot return. */
data class NavEvent(val route: String, val clearBackStack: Boolean = false)
