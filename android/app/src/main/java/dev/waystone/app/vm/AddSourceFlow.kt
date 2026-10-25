package dev.waystone.app.vm

import dev.waystone.data.config.EmulatorPreset
import dev.waystone.data.config.ROM_KEYED_ADAPTER
import dev.waystone.data.config.SourceFolder

sealed interface AddSourceStep {
    data object Idle : AddSourceStep
    data object ChoosePreset : AddSourceStep

    /** preset null = Manual (the adapter/system dialog). */
    data class PickRoms(val preset: EmulatorPreset?) : AddSourceStep
    data class ConfirmManual(val romUri: String) : AddSourceStep
    data class ExplainSaves(val preset: EmulatorPreset, val romUri: String) : AddSourceStep
    data class PickSaves(val preset: EmulatorPreset, val romUri: String) : AddSourceStep
    data class Done(val folder: SourceFolder) : AddSourceStep
}

object AddSourceFlow {
    fun onPresetChosen(preset: EmulatorPreset?): AddSourceStep.PickRoms = AddSourceStep.PickRoms(preset)

    fun onRomsPicked(step: AddSourceStep.PickRoms, uri: String?): AddSourceStep {
        val preset = step.preset
        return when {
            uri == null -> AddSourceStep.Idle
            preset == null -> AddSourceStep.ConfirmManual(uri)
            preset.asksForSaves -> AddSourceStep.ExplainSaves(preset, uri)
            else -> presetSource(uri, preset)
        }
    }

    fun onExplained(step: AddSourceStep.ExplainSaves, pickSaves: Boolean): AddSourceStep =
        if (pickSaves) AddSourceStep.PickSaves(step.preset, step.romUri)
        else presetSource(step.romUri, step.preset)

    fun onSavesPicked(step: AddSourceStep.PickSaves, uri: String?): AddSourceStep =
        presetSource(step.romUri, step.preset, uri)

    fun onManualConfirmed(step: AddSourceStep.ConfirmManual, adapter: String, system: String): AddSourceStep =
        AddSourceStep.Done(SourceFolder(step.romUri, adapter, if (adapter == ROM_KEYED_ADAPTER) "" else system))

    private fun presetSource(romUri: String, preset: EmulatorPreset, saveUri: String? = null) =
        AddSourceStep.Done(SourceFolder(romUri, ROM_KEYED_ADAPTER, "", saveUri = saveUri, preset = preset.id))
}
