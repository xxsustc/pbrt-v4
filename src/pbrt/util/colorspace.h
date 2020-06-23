// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// It is licensed under the BSD license; see the file LICENSE.txt
// SPDX: BSD-3-Clause

#ifndef PBRT_COLORSPACE_H
#define PBRT_COLORSPACE_H

#include <pbrt/pbrt.h>

#include <pbrt/util/math.h>
#include <pbrt/util/spectrum.h>
#include <pbrt/util/vecmath.h>

#include <string>

namespace pbrt {

/* We always assume that the opto-electronic transfer function (OETF) and
   electro-optical transfer function (EOTF) are the identity mappings at
   this point; we convert to linear upon reading images from disk (and
   require linear for color values specified elsewhere).  But should
   discuss what these are, introduce the sRGB gamma curve under the
   umbrella of those, etc.
*/
class RGBColorSpace {
  public:
    // Chromaticities of the r, g, and b primaries and the white point.
    RGBColorSpace(Point2f r, Point2f g, Point2f b, Point2f w, SpectrumHandle illuminant,
                  const RGBToSpectrumTable *rgbToSpectrumTable, Allocator alloc);

    PBRT_CPU_GPU
    RGB ToRGB(const XYZ &xyz) const { return Mul<RGB>(RGBFromXYZ, xyz); }
    PBRT_CPU_GPU
    XYZ ToXYZ(const RGB &rgb) const { return Mul<XYZ>(XYZFromRGB, rgb); }
    PBRT_CPU_GPU
    RGBSigmoidPolynomial ToRGBCoeffs(const RGB &rgb) const;

    // TODO: these could run on the device as well, but need to deal with
    // the constants for the Bradford matrix and its inverse.
    SquareMatrix<3> ColorCorrectionMatrixForxy(Float x, Float y) const;
    SquareMatrix<3> ColorCorrectionMatrixForXYZ(const XYZ &xyz) const {
        return ColorCorrectionMatrixForxy(xyz[0] / (xyz[0] + xyz[1] + xyz[2]),
                                          xyz[1] / (xyz[0] + xyz[1] + xyz[2]));
    }

    static void Init(Allocator alloc);
    static const RGBColorSpace *GetNamed(const std::string &name);

    static const RGBColorSpace *ACES2065_1;
    static const RGBColorSpace *Rec2020;  // UHDTV
    static const RGBColorSpace *sRGB;

    PBRT_CPU_GPU
    bool operator==(const RGBColorSpace &cs) const {
        return (r == cs.r && g == cs.g && b == cs.b && w == cs.w &&
                rgbToSpectrumTable == cs.rgbToSpectrumTable);
    }
    PBRT_CPU_GPU
    bool operator!=(const RGBColorSpace &cs) const {
        return (r != cs.r || g != cs.g || b != cs.b || w != cs.w ||
                rgbToSpectrumTable != cs.rgbToSpectrumTable);
    }

    static const RGBColorSpace *Lookup(Point2f r, Point2f g, Point2f b, Point2f w);

    Point2f r, g, b, w;  // xy chromaticities
    const DenselySampledSpectrum illuminant;

    std::string ToString() const;

  private:
    PBRT_CPU_GPU
    friend SquareMatrix<3> ConvertRGBColorSpace(const RGBColorSpace &from,
                                                const RGBColorSpace &to);

    SquareMatrix<3> XYZFromRGB, RGBFromXYZ;
    const RGBToSpectrumTable *rgbToSpectrumTable;
};

#ifdef PBRT_BUILD_GPU_RENDERER
extern PBRT_CONST RGBColorSpace *RGBColorSpace_ACES2065_1;
extern PBRT_CONST RGBColorSpace *RGBColorSpace_Rec2020;
extern PBRT_CONST RGBColorSpace *RGBColorSpace_sRGB;
#endif

SquareMatrix<3> ConvertRGBColorSpace(const RGBColorSpace &from, const RGBColorSpace &to);

}  // namespace pbrt

#endif  // PBRT_COLORSPACE_H