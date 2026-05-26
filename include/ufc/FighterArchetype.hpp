#pragma once

#include "ufc/FighterCareerStats.hpp"

#include <cstdint>
#include <optional>
#include <string>

struct sqlite3;

namespace ufc {

/// Fighting style bucket derived from career round stats.
///
/// | Archetype                 | Profile |
/// |---------------------------|---------|
/// | Pressure Striker          | Works at range; high distance striking, limited grappling |
/// | Ground Control Specialist | Ground-control-first; racks up top control time, not a submission hunter |
/// | Ground Finisher           | Submission threat, extended control, ground offense |
/// | All-Around Fighter        | High pace — volume striking combined with wrestling/clinch pressure |
/// | Counter Striker           | Defensive striking — strong defense, lower absorbed volume |
enum class FighterArchetype {
    PressureStriker,
    GroundControlSpecialist,
    GroundFinisher,
    AllAroundFighter,
    CounterStriker,
};

const char* toString(FighterArchetype archetype);
std::optional<FighterArchetype> parseArchetype(const std::string& label);

/// Returns nullopt when there is insufficient round data to classify reliably.
std::optional<FighterArchetype> classifyArchetype(const FighterCareerStats& stats);
std::optional<FighterArchetype> classifyArchetypeForFighter(sqlite3* db, int64_t fighter_id);

}  // namespace ufc
