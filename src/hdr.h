#pragma once
#include "capture.h"
namespace clip {
float halfFloat(quint16);
double pqEncode(double nits);
double pqDecode(double signal);
// WGC FP16 surfaces use linear scRGB: 1.0 = 80 nits, BT.709 primaries.
// HDR output is limited-range BT.2020/PQ; SDR conversion is explicit Hable/BT.709.
void convertFloatFrame(const CapturedFrame &, AVFrame *destination, bool preserveHdr);
QString sdrToneMapFilter();
QStringList colorArguments(const MediaInfo &, bool sdr = false);
} // namespace clip
