#pragma once

#include "ufc/AnalysisTypes.hpp"

struct sqlite3;

namespace ufc {

MomentumBreakdown buildMomentumBreakdown(sqlite3* db, int64_t fighter_id);
ResumeBreakdown buildResumeBreakdown(sqlite3* db, int64_t fighter_id);

}  // namespace ufc
