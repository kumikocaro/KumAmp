#pragma once

namespace kumamp
{
/** Registers the blocks that depend on JUCE / NAM (NAM amp, IR loader) with the
    core BlockFactory. Idempotent; called by GridEngine. */
void registerJuceBlocks();
} // namespace kumamp
