#include "FrameData.h"
int FrameAdapter_SmokeTest() { FrameData::Reload(); CView v; FrameData::ApplyView(v); std::vector<CEntity> es; return FrameData::BuildEntities(es, v); }
