#pragma once

namespace VisibilityFixer
{
    void Install();
    void ProcessFixes(bool a_force = false);
    void ClearFixedActors();
    bool IsActorInOStimScene(RE::Actor* a_actor);
}
