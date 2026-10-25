package dev.waystone.app.vm

import dev.waystone.data.config.EmulatorPreset
import dev.waystone.data.config.SourceFolder
import org.junit.Assert.assertEquals
import org.junit.Test

class AddSourceFlowTest {
    @Test fun `retroarch asks for saves then builds a preset source`() {
        val roms = AddSourceFlow.onPresetChosen(EmulatorPreset.RETROARCH)
        val explain = AddSourceFlow.onRomsPicked(roms, "content://roms")
        assertEquals(AddSourceStep.ExplainSaves(EmulatorPreset.RETROARCH, "content://roms"), explain)
        val pick = AddSourceFlow.onExplained(explain as AddSourceStep.ExplainSaves, pickSaves = true)
        assertEquals(
            AddSourceStep.Done(SourceFolder("content://roms", "rom_keyed", "", "content://saves", "retroarch")),
            AddSourceFlow.onSavesPicked(pick as AddSourceStep.PickSaves, "content://saves"),
        )
    }

    @Test fun `melonDS needs only the ROM folder, cancelled saves picker keeps ROM-only`() {
        assertEquals(
            AddSourceStep.Done(SourceFolder("content://r", "rom_keyed", "", null, "melonds")),
            AddSourceFlow.onRomsPicked(AddSourceFlow.onPresetChosen(EmulatorPreset.MELONDS), "content://r"),
        )
        val pick = AddSourceStep.PickSaves(EmulatorPreset.DRASTIC, "content://r")
        assertEquals(AddSourceStep.Done(SourceFolder("content://r", "rom_keyed", "", null, "drastic")), AddSourceFlow.onSavesPicked(pick, null))
    }

    @Test fun `manual goes through the adapter dialog, cancelled ROM picker resets`() {
        val manual = AddSourceFlow.onRomsPicked(AddSourceFlow.onPresetChosen(null), "content://j")
        assertEquals(AddSourceStep.ConfirmManual("content://j"), manual)
        assertEquals(
            AddSourceStep.Done(SourceFolder("content://j", "jksv", "switch")),
            AddSourceFlow.onManualConfirmed(manual as AddSourceStep.ConfirmManual, "jksv", "switch"),
        )
        assertEquals(AddSourceStep.Idle, AddSourceFlow.onRomsPicked(AddSourceStep.PickRoms(null), null))
    }
}
