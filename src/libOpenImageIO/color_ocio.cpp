// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <tsl/robin_map.h>

#include <OpenImageIO/Imath.h>

#include <OpenImageIO/color.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagebufalgo_util.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/sysutil.h>

#include "imageio_pvt.h"

#define MAKE_OCIO_VERSION_HEX(maj, min, patch) \
    (((maj) << 24) | ((min) << 16) | (patch))

#include <OpenColorIO/OpenColorIO.h>

namespace OCIO = OCIO_NAMESPACE;


OIIO_NAMESPACE_3_1_BEGIN

namespace {



static constexpr const char kInteropIdentitiesConfig[] = R"INTEROP(
ocio_profile_version: 2.3
# Keep in sync with OpenImageIO minimum OCIO version

name: interop-identities-config-v0.26.1.8

description: |
  Interop Identities Reference Config
  -----------------------------------
  A minimal config defining color spaces associated with known CIF and OCIO-builtin 
  color space interop_ids, backported for the minimum version of OCIO currently 
  allowed by OpenImageIO. 

  There is a 1:1 relationship between each color space's name and interop_id.
  
  This config provides functionally equivalent color spaces current with:
    - core-display-config-v1.0.0
    - core-renderer-config-v1.0.0
    - ocio://studio-config-v4.0.0_aces-v2.0_ocio-v2.5

    Note: This config does not include the following color spaces:
      - ocio:applelog_rec2020_scene
      - ocio:applelog_applewg_scene

  Note (Jan 8, 2024): 
    Once we require OCIO v2.5+, we can remove almost all of these color spaces.
    In the future, this config will consist of only the camera color spaces
    we want to backport from future studio configs, and / or community-provided
    "oiio:"-namespaced color spaces not available in OCIO builtins.

roles:
  aces_interchange: lin_ap0_scene
  cie_xyz_d65_interchange: lin_ciexyzd65_display
  compositing_log: ocio:acescct_ap1_scene
  color_timing: ocio:acescct_ap1_scene
  scene_linear: lin_ap1_scene
  default: data


file_rules:
  - !<Rule> {name: Default, colorspace: default}

shared_views:
  - !<View> {name: None, colorspace: data}

displays:
  g24_rec709_display:
    - !<Views> [None]

default_view_transform: scene_to_display_bridge

view_transforms:
  - !<ViewTransform>
    name: scene_to_display_bridge
    from_scene_reference: !<BuiltinTransform> {style: UTILITY - ACES-AP0_to_CIE-XYZ-D65_BFD}

colorspaces:
  - !<ColorSpace>
    name: lin_ap0_scene
    interop_id: lin_ap0_scene
    encoding: scene-linear

  - !<ColorSpace>
    name: lin_ap1_scene
    interop_id: lin_ap1_scene
    encoding: scene-linear
    to_scene_reference: !<BuiltinTransform> {style: ACEScg_to_ACES2065-1}

  - !<ColorSpace>
    name: lin_rec709_scene
    interop_id: lin_rec709_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: rec709_to_ap0, matrix: [0.439632981919491, 0.382988698151554, 0.177378319928955, 0, 0.0897764429588424, 0.813439428748981, 0.0967841282921771, 0, 0.0175411703831727, 0.111546553302387, 0.87091227631444, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: lin_p3d65_scene
    interop_id: lin_p3d65_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: p3d65_to_ap0, matrix: [0.518933487597981, 0.28625658638669, 0.194809926015329, 0, 0.0738593830470598, 0.819845163936986, 0.106295453015954, 0, -0.000307011368446647, 0.0438070502536223, 0.956499961114824, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: lin_rec2020_scene
    interop_id: lin_rec2020_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: rec2020_to_ap0, matrix: [0.679085634706912, 0.157700914643159, 0.163213450649929, 0, 0.0460020030800595, 0.859054673002908, 0.0949433239170327, 0, -0.000573943187616196, 0.0284677684080264, 0.97210617477959, 0, 0, 0, 0, 1]}
  
  - !<ColorSpace>
    name: lin_adobergb_scene
    interop_id: lin_adobergb_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: adobe1998_to_ap0, matrix: [0.614763305501725, 0.200243702572018, 0.184992991926256, 0, 0.125539404683864, 0.773521622216629, 0.100938973099507, 0, 0.0245287963611042, 0.0671715435381276, 0.908299660100768, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: lin_ciexyzd65_scene
    interop_id: lin_ciexyzd65_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: ciexyzd65_to_ap0, matrix: [1.0634954914942, 0.00640891019711789, -0.0158067866176054, 0, -0.492074127923892, 1.36822340747333, 0.0913370883144736, 0, -0.00281646163925351, 0.00464417105680067, 0.916418574593656, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:acescc_ap1_scene
    interop_id: ocio:acescc_ap1_scene
    encoding: log
    to_scene_reference: !<BuiltinTransform> {style: ACEScc_to_ACES2065-1}
  
  - !<ColorSpace>
    name: ocio:acescct_ap1_scene
    interop_id: ocio:acescct_ap1_scene
    encoding: log
    to_scene_reference: !<BuiltinTransform> {style: ACEScct_to_ACES2065-1}

  - !<ColorSpace>
    name: ocio:adx10_apd_scene
    interop_id: ocio:adx10_apd_scene
    encoding: log
    to_scene_reference: !<BuiltinTransform> {style: ADX10_to_ACES2065-1}

  - !<ColorSpace>
    name: ocio:adx16_apd_scene
    interop_id: ocio:adx16_apd_scene
    encoding: log
    to_scene_reference: !<BuiltinTransform> {style: ADX16_to_ACES2065-1}

  - !<ColorSpace>
    name: ocio:lin_applewg_scene
    interop_id: ocio:lin_applewg_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: applewg_to_ap0, matrix: [0.694961049318096, 0.241405268785364, 0.06363368189654, 0, 0.0473627464149325, 1.00429592505428, -0.0516586714692158, 0, -0.021989789359883, -0.0289891049714743, 1.05097889433136, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:arrilogc3_awg3_scene
    interop_id: ocio:arrilogc3_awg3_scene
    encoding: log
    to_scene_reference: !<BuiltinTransform> {style: ARRI_ALEXA-LOGC-EI800-AWG_to_ACES2065-1}

  - !<ColorSpace>
    name: ocio:lin_awg3_scene
    interop_id: ocio:lin_awg3_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: awg3_to_ap0, matrix: [0.680205505106279, 0.236136601606481, 0.0836578932872398, 0, 0.0854149797421404, 1.01747087860704, -0.102885858349182, 0, 0.00205652166929683, -0.0625625003847921, 1.06050597871549, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:arrilogc4_awg4_scene
    interop_id: ocio:arrilogc4_awg4_scene
    encoding: log
    to_scene_reference: !<BuiltinTransform> {style: ARRI_LOGC4_to_ACES2065-1}

  - !<ColorSpace>
    name: ocio:lin_awg4_scene
    interop_id: ocio:lin_awg4_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: awg4_to_ap0, matrix: [0.750957362824734, 0.144422786709757, 0.104619850465509, 0, 0.000821837079380207, 1.007397584885, -0.00821942196438358, 0, -0.000499952143533471, -0.000854177231436971, 1.00135412937497, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name:  ocio:bmdfilm5_wg5_scene
    interop_id:  ocio:bmdfilm5_wg5_scene
    encoding: log
    to_scene_reference: !<GroupTransform>
      children:
        - !<LogCameraTransform> {name: bmdfilm5, base: 2.71828182845905, log_side_slope: 0.0869287606549122, log_side_offset: 0.530013339229194, lin_side_offset: 0.00549407243225781, lin_side_break: 0.005, direction: inverse}
        - !<MatrixTransform> {name: bmdwg5_to_ap0, matrix: [0.647091325580708, 0.242595385134207, 0.110313289285085, 0, 0.0651915997328519, 1.02504756760476, -0.0902391673376125, 0, -0.0275570729194699, -0.0805887097177784, 1.10814578263725, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:lin_bmdwg5_scene
    interop_id: ocio:lin_bmdwg5_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: bmdwg5_to_ap0, matrix: [0.647091325580708, 0.242595385134207, 0.110313289285085, 0, 0.0651915997328519, 1.02504756760476, -0.0902391673376125, 0, -0.0275570729194699, -0.0805887097177784, 1.10814578263725, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:davinci_dwg_scene
    interop_id: ocio:davinci_dwg_scene
    encoding: log
    to_scene_reference: !<GroupTransform>
      children:
        - !<LogCameraTransform> {name: davinciintermediate, log_side_slope: 0.07329248, log_side_offset: 0.51304736, lin_side_offset: 0.0075, lin_side_break: 0.00262409, linear_slope: 10.44426855, direction: inverse}
        - !<MatrixTransform> {name: dwg_to_ap0, matrix: [0.748270290272981, 0.167694659554328, 0.0840350501726906, 0, 0.0208421234689102, 1.11190474268894, -0.132746866157851, 0, -0.0915122574225729, -0.127746712807307, 1.21925897022988, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:lin_dwg_scene
    interop_id: ocio:lin_dwg_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: dwg_to_ap0, matrix: [0.748270290272981, 0.167694659554328, 0.0840350501726906, 0, 0.0208421234689102, 1.11190474268894, -0.132746866157851, 0, -0.0915122574225729, -0.127746712807307, 1.21925897022988, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:canonlog3_cgamutd55_scene
    interop_id: ocio:canonlog3_cgamutd55_scene
    encoding: log
    to_scene_reference: !<BuiltinTransform> {style: CANON_CLOG3-CGAMUT_to_ACES2065-1}

  - !<ColorSpace>
    name: ocio:canonlog2_cgamutd55_scene
    interop_id: ocio:canonlog2_cgamutd55_scene
    encoding: log
    to_scene_reference: !<BuiltinTransform> {style: CANON_CLOG2-CGAMUT_to_ACES2065-1}

  - !<ColorSpace>
    name: ocio:lin_cgamutd55_scene
    interop_id: ocio:lin_cgamutd55_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: cgamutd65_to_ap0, matrix: [0.763064454775734, 0.14902116113706, 0.0879143840872056, 0, 0.00365745670512393, 1.10696038037622, -0.110617837081339, 0, -0.0094077940457189, -0.218383304989987, 1.22779109903571, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:djilog_dgamut_scene
    interop_id: ocio:djilog_dgamut_scene
    encoding: log
    to_scene_reference: !<GroupTransform>
      children:
        - !<LogCameraTransform> {name: djilog, base: 10, log_side_slope: 0.256662970719888, log_side_offset: 0.58455504907396, lin_side_slope: 0.9892, lin_side_offset: 0.0108, lin_side_break: 0.00758078675, direction: inverse}
        - !<MatrixTransform> {name: dgamut_to_ap0, matrix: [0.691279245585754, 0.214382527745956, 0.0943382266682902, 0, 0.0662224037667752, 1.0116160801876, -0.0778384839543733, 0, -0.0172985410341745, -0.0773788501012682, 1.09467739113544, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:lin_dgamut_scene
    interop_id: ocio:lin_dgamut_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: dgamut_to_ap0, matrix: [0.691279245585754, 0.214382527745956, 0.0943382266682902, 0, 0.0662224037667752, 1.0116160801876, -0.0778384839543733, 0, -0.0172985410341745, -0.0773788501012682, 1.09467739113544, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:vlog_vgamut_scene
    interop_id: ocio:vlog_vgamut_scene
    encoding: log
    to_scene_reference: !<BuiltinTransform> {style: PANASONIC_VLOG-VGAMUT_to_ACES2065-1}
  
  - !<ColorSpace>
    name: ocio:lin_vgamut_scene
    interop_id: ocio:lin_vgamut_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: vgamut_to_ap0, matrix: [0.72461670413153, 0.166915288193706, 0.108468007674764, 0, 0.021390245413146, 0.984908155703054, -0.00629840111620089, 0, -0.00923556287076561, -0.00105690563900513, 1.01029246850977, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:redlog3g10_rwg_scene
    interop_id: ocio:redlog3g10_rwg_scene
    encoding: log
    to_scene_reference: !<BuiltinTransform> {style: RED_LOG3G10-RWG_to_ACES2065-1}

  - !<ColorSpace>
    name: ocio:lin_rwg_scene
    interop_id: ocio:lin_rwg_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: rwg_to_ap0, matrix: [0.785058804068092, 0.0838587565440846, 0.131082439387823, 0, 0.0231738348454756, 1.08789754919233, -0.111071384037806, 0, -0.0737604353682082, -0.314590072290208, 1.38835050765842, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:slog3_sgamut3_scene
    interop_id: ocio:slog3_sgamut3_scene
    encoding: log
    to_scene_reference: !<BuiltinTransform> {style: SONY_SLOG3-SGAMUT3_to_ACES2065-1}

  - !<ColorSpace>
    name: ocio:slog3_sgamut3cine_scene
    interop_id: ocio:slog3_sgamut3cine_scene
    encoding: log
    to_scene_reference: !<BuiltinTransform> {style: SONY_SLOG3-SGAMUT3.CINE_to_ACES2065-1}

  - !<ColorSpace>
    name: ocio:slog3_sgamut3venice_scene
    interop_id: ocio:slog3_sgamut3venice_scene
    encoding: log
    to_scene_reference: !<BuiltinTransform> {style: SONY_SLOG3-SGAMUT3-VENICE_to_ACES2065-1}

  - !<ColorSpace>
    name: ocio:slog3_sgamut3cinevenice_scene
    interop_id: ocio:slog3_sgamut3cinevenice_scene
    encoding: log
    to_scene_reference: !<BuiltinTransform> {style: SONY_SLOG3-SGAMUT3.CINE-VENICE_to_ACES2065-1}

  - !<ColorSpace>
    name: ocio:lin_sgamut3_scene
    interop_id: ocio:lin_sgamut3_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: sgamut3_to_ap0, matrix: [0.75298259539984, 0.143370216235557, 0.103647188364603, 0, 0.0217076974414429, 1.01531883550528, -0.0370265329467195, 0, -0.00941605274963355, 0.00337041785882367, 1.00604563489081, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:lin_sgamut3cine_scene
    interop_id: ocio:lin_sgamut3cine_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: sgamut3cine_to_ap0, matrix: [0.638788667185978, 0.272351433711262, 0.0888598991027595, 0, -0.00391590602528224, 1.0880732308974, -0.0841573248721177, 0, -0.0299072021239151, -0.0264325799101947, 1.05633978203411, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:lin_sgamut3venice_scene
    interop_id: ocio:lin_sgamut3venice_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: sgamut3venice_to_ap0, matrix: [0.793329741146434, 0.089078625620677, 0.117591633232888, 0, 0.0155810585252582, 1.03271230692988, -0.0482933654551394, 0, -0.0188647477991488, 0.0127694120973433, 1.00609533570181, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:lin_sgamut3cinevenice_scene
    interop_id: ocio:lin_sgamut3cinevenice_scene
    encoding: scene-linear
    to_scene_reference: !<MatrixTransform> {name: sgamut3cinevenice_to_ap0, matrix: [0.674257092126512, 0.220571735923397, 0.10517117195009, 0, -0.00931360607857167, 1.10595886142466, -0.0966452553460855, 0, -0.0382090673002312, -0.017938376600236, 1.05614744390047, 0, 0, 0, 0, 1]}

  
  - !<ColorSpace>
    name: g18_rec709_scene
    interop_id: g18_rec709_scene
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentTransform> {name: g18, value: 1.8, style: pass_thru}
        - !<MatrixTransform> {name: rec709_to_ap0, matrix: [0.439632981919491, 0.382988698151554, 0.177378319928955, 0, 0.0897764429588424, 0.813439428748981, 0.0967841282921771, 0, 0.0175411703831727, 0.111546553302387, 0.87091227631444, 0, 0, 0, 0, 1]}
  
  - !<ColorSpace>
    name: g22_rec709_scene
    interop_id: g22_rec709_scene
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentTransform> {name: g22, value: 2.2, style: pass_thru}
        - !<MatrixTransform> {name: rec709_to_ap0, matrix: [0.439632981919491, 0.382988698151554, 0.177378319928955, 0, 0.0897764429588424, 0.813439428748981, 0.0967841282921771, 0, 0.0175411703831727, 0.111546553302387, 0.87091227631444, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: srgb_rec709_scene
    aliases: [sRGB - Texture]
    interop_id: srgb_rec709_scene
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentWithLinearTransform> {name: srgb, gamma: 2.4, offset: 0.055}
        - !<MatrixTransform> {name: rec709_to_ap0, matrix: [0.439632981919491, 0.382988698151554, 0.177378319928955, 0, 0.0897764429588424, 0.813439428748981, 0.0967841282921771, 0, 0.0175411703831727, 0.111546553302387, 0.87091227631444, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: srgb_ap1_scene
    interop_id: srgb_ap1_scene
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentWithLinearTransform> {name: srgb, gamma: 2.4, offset: 0.055}
        - !<BuiltinTransform> {style: ACEScg_to_ACES2065-1}
  
  - !<ColorSpace>
    name: g22_ap1_scene
    interop_id: g22_ap1_scene
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentTransform> {name: g22,value: 2.2, style: pass_thru}
        - !<BuiltinTransform> {style: ACEScg_to_ACES2065-1}
        
  - !<ColorSpace>
    name: srgb_p3d65_scene
    interop_id: srgb_p3d65_scene
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentWithLinearTransform> {name: srgb, gamma: 2.4, offset: 0.055}
        - !<MatrixTransform> {name: p3d65_to_ap0, matrix: [0.518933487597981, 0.28625658638669, 0.194809926015329, 0, 0.0738593830470598, 0.819845163936986, 0.106295453015954, 0, -0.000307011368446647, 0.0438070502536223, 0.956499961114824, 0, 0, 0, 0, 1]}
  
  - !<ColorSpace>
    name: g22_adobergb_scene
    interop_id: g22_adobergb_scene
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentTransform> {name: adobeg22, value: 2.19921875, style: pass_thru}
        - !<MatrixTransform> {name: adobe1998_to_ap0, matrix: [0.614763305501725, 0.200243702572018, 0.184992991926256, 0, 0.125539404683864, 0.773521622216629, 0.100938973099507, 0, 0.0245287963611042, 0.0671715435381276, 0.908299660100768, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: ocio:g24_rec709_scene
    interop_id: ocio:g24_rec709_scene
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentTransform> {name: g24, value: 2.4, style: pass_thru}
        - !<MatrixTransform> {name: rec709_to_ap0, matrix: [0.439632981919491, 0.382988698151554, 0.177378319928955, 0, 0.0897764429588424, 0.813439428748981, 0.0967841282921771, 0, 0.0175411703831727, 0.111546553302387, 0.87091227631444, 0, 0, 0, 0, 1]}
  
  - !<ColorSpace>
    name: ocio:itu709_rec709_scene
    interop_id: ocio:itu709_rec709_scene
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentWithLinearTransform> {name: itu709, gamma: 2.22222222222222, offset: 0.099}
        - !<MatrixTransform> {name: rec709_to_ap0, matrix: [0.439632981919491, 0.382988698151554, 0.177378319928955, 0, 0.0897764429588424, 0.813439428748981, 0.0967841282921771, 0, 0.0175411703831727, 0.111546553302387, 0.87091227631444, 0, 0, 0, 0, 1]}
        
display_colorspaces:
  - !<ColorSpace>
    name: lin_ciexyzd65_display
    interop_id: lin_ciexyzd65_display
    encoding: display-linear

  - !<ColorSpace>
    name: lin_rec709_display
    interop_id: lin_rec709_display
    encoding: display-linear
    from_display_reference: !<MatrixTransform> {name: ciexyzd65_to_rec709, matrix: [3.24096994190452, -1.53738317757009, -0.498610760293003, 0, -0.96924363628088, 1.87596750150772, 0.0415550574071756, 0, 0.0556300796969936, -0.203976958888976, 1.05697151424288, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: lin_p3d65_display
    interop_id: lin_p3d65_display
    encoding: display-linear
    from_display_reference: !<MatrixTransform> {name: ciexyzd65_to_p3d65, matrix: [2.49349691194143, -0.931383617919124, -0.402710784450717, 0, -0.829488969561575, 1.76266406031835, 0.0236246858419436, 0, 0.0358458302437845, -0.0761723892680418, 0.956884524007688, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: lin_rec2020_display
    interop_id: lin_rec2020_display
    encoding: display-linear
    from_display_reference: !<MatrixTransform> {name: ciexyzd65_to_rec2020, matrix: [1.71665118797127, -0.355670783776392, -0.25336628137366, 0, -0.666684351832489, 1.61648123663494, 0.0157685458139111, 0, 0.0176398574453108, -0.0427706132578085, 0.942103121235474, 0, 0, 0, 0, 1]}

  - !<ColorSpace>
    name: srgb_rec709_display
    interop_id: srgb_rec709_display
    encoding: sdr-video
    from_display_reference: !<GroupTransform>
      children:
        - !<MatrixTransform> {name: ciexyzd65_to_rec709, matrix: [3.24096994190452, -1.53738317757009, -0.498610760293003, 0, -0.96924363628088, 1.87596750150772, 0.0415550574071756, 0, 0.0556300796969936, -0.203976958888976, 1.05697151424288, 0, 0, 0, 0, 1]}
        - !<ExponentWithLinearTransform> {name: srgb, gamma: 2.4, offset: 0.055, style: mirror, direction: inverse}

  - !<ColorSpace>
    name: g24_rec709_display
    interop_id: g24_rec709_display
    encoding: sdr-video
    from_display_reference: !<GroupTransform>
      children:
        - !<MatrixTransform> {name: ciexyzd65_to_rec709, matrix: [3.24096994190452, -1.53738317757009, -0.498610760293003, 0, -0.96924363628088, 1.87596750150772, 0.0415550574071756, 0, 0.0556300796969936, -0.203976958888976, 1.05697151424288, 0, 0, 0, 0, 1]}
        - !<ExponentTransform> {name: g24, value: 2.4, style: mirror, direction: inverse}

  - !<ColorSpace>
    name: srgb_p3d65_display
    interop_id: srgb_p3d65_display
    encoding: sdr-video
    from_display_reference: !<GroupTransform>
      children:
        - !<MatrixTransform> {name: ciexyzd65_to_p3d65, matrix: [2.49349691194143, -0.931383617919124, -0.402710784450717, 0, -0.829488969561575, 1.76266406031835, 0.0236246858419436, 0, 0.0358458302437845, -0.0761723892680418, 0.956884524007688, 0, 0, 0, 0, 1]}
        - !<ExponentWithLinearTransform> {name: srgb, gamma: 2.4, offset: 0.055, style: mirror, direction: inverse}

  - !<ColorSpace>
    name: srgbe_p3d65_display
    interop_id: srgbe_p3d65_display
    encoding: hdr-video
    from_display_reference: !<GroupTransform>
      children:
        - !<MatrixTransform> {name: ciexyzd65_to_p3d65, matrix: [2.49349691194143, -0.931383617919124, -0.402710784450717, 0, -0.829488969561575, 1.76266406031835, 0.0236246858419436, 0, 0.0358458302437845, -0.0761723892680418, 0.956884524007688, 0, 0, 0, 0, 1]}
        - !<ExponentWithLinearTransform> {name: srgb, gamma: 2.4, offset: 0.055, style: mirror, direction: inverse}

  - !<ColorSpace>
    name: pq_p3d65_display
    interop_id: pq_p3d65_display
    encoding: hdr-video
    from_display_reference: !<BuiltinTransform> {style: DISPLAY - CIE-XYZ-D65_to_ST2084-P3-D65}

  - !<ColorSpace>
    name: pq_rec2020_display
    interop_id: pq_rec2020_display
    encoding: hdr-video
    from_display_reference: !<BuiltinTransform> {style: DISPLAY - CIE-XYZ-D65_to_REC.2100-PQ}

  - !<ColorSpace>
    name: hlg_rec2020_display
    interop_id: hlg_rec2020_display
    encoding: hdr-video
    from_display_reference: !<BuiltinTransform> {style: DISPLAY - CIE-XYZ-D65_to_REC.2100-HLG-1000nit}

  - !<ColorSpace>
    name: g22_rec709_display
    interop_id: g22_rec709_display
    encoding: sdr-video
    from_display_reference: !<GroupTransform>
      children:
        - !<MatrixTransform> {name: ciexyzd65_to_rec709, matrix: [3.24096994190452, -1.53738317757009, -0.498610760293003, 0, -0.96924363628088, 1.87596750150772, 0.0415550574071756, 0, 0.0556300796969936, -0.203976958888976, 1.05697151424288, 0, 0, 0, 0, 1]}
        - !<ExponentTransform> {name: g22, value: 2.2, style: mirror, direction: inverse}

  - !<ColorSpace>
    name: g22_adobergb_display
    interop_id: g22_adobergb_display
    encoding: sdr-video
    from_display_reference: !<GroupTransform>
      children:
        - !<MatrixTransform> {name: ciexyzd65_to_adobergb, matrix: [2.04158790381075, -0.56500697427886, -0.34473135077833, 0, -0.96924363628088, 1.87596750150772, 0.0415550574071756, 0, 0.0134442806320311, -0.118362392231018, 1.01517499439121, 0, 0, 0, 0, 1]}
        - !<ExponentTransform> {name: adobeg22, value: 2.19921875, style: mirror, direction: inverse}

  - !<ColorSpace>
    name: g26_p3d65_display
    interop_id: g26_p3d65_display
    encoding: sdr-video
    from_display_reference: !<GroupTransform>
      children:
        - !<MatrixTransform> {name: ciexyzd65_to_p3d65, matrix: [2.49349691194143, -0.931383617919124, -0.402710784450717, 0, -0.829488969561575, 1.76266406031835, 0.0236246858419436, 0, 0.0358458302437845, -0.0761723892680418, 0.956884524007688, 0, 0, 0, 0, 1]}
        - !<ExponentTransform> {name: g26, value: 2.6, style: mirror, direction: inverse}

  - !<ColorSpace>
    name: g26_xyzd65_display
    interop_id: g26_xyzd65_display
    encoding: sdr-video
    from_display_reference: !<GroupTransform>
      children:
        - !<RangeTransform> {name: dci_white_headroom, min_in_value: 0, max_in_value: 1, min_out_value: 0, max_out_value: 0.916555279740309, style: noClamp}
        - !<ExponentTransform> {name: g26, value: 2.6, style: mirror, direction: inverse}

  - !<ColorSpace>
    name: pq_xyzd65_display
    interop_id: pq_xyzd65_display
    encoding: hdr-video
    from_display_reference: !<BuiltinTransform> {style: CURVE - LINEAR_to_ST-2084}
  
  - !<ColorSpace>
    name: data
    interop_id: data
    encoding: data
    isdata: true

named_transforms:
  - !<NamedTransform>
    name: oiio:full_to_narrow_range
    transform: !<RangeTransform> {name: full to legal scaling, min_in_value: 0, max_in_value: 1, min_out_value: 0.062561094819159, max_out_value: 0.918866080156403, style: noClamp}

  - !<NamedTransform>
    name: oiio:narrow_to_full_range
    inverse_transform: !<RangeTransform> {name: full to legal scaling, min_in_value: 0, max_in_value: 1, min_out_value: 0.062561094819159, max_out_value: 0.918866080156403, style: noClamp}

)INTEROP";


// Some test colors we use to interrogate transformations
static const int n_test_colors = 5;
static const Imath::C3f test_colors[n_test_colors]
    = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 1, 1 }, { 0.5, 0.5, 0.5 } };
}  // namespace


#if 1 || !defined(NDEBUG) /* allow color configuration debugging */
static bool colordebug = Strutil::stoi(Sysutil::getenv("OIIO_DEBUG_COLOR"))
                         || Strutil::stoi(Sysutil::getenv("OIIO_DEBUG_ALL"));
#    define DBG(...)    \
        if (colordebug) \
        Strutil::print(__VA_ARGS__)
#else
#    define DBG(...)
#endif


static int disable_ocio = Strutil::stoi(Sysutil::getenv("OIIO_DISABLE_OCIO"));
static int disable_builtin_configs = Strutil::stoi(
    Sysutil::getenv("OIIO_DISABLE_BUILTIN_OCIO_CONFIGS"));
static OCIO::ConstConfigRcPtr ocio_current_config;



const ColorConfig&
ColorConfig::default_colorconfig()
{
    static ColorConfig config;
    return config;
}



// Class used as the key to index color processors in the cache.
class ColorProcCacheKey {
public:
    ColorProcCacheKey(ustring in, ustring out, ustring key = ustring(),
                      ustring val = ustring(), ustring looks = ustring(),
                      ustring display = ustring(), ustring view = ustring(),
                      ustring file           = ustring(),
                      ustring namedtransform = ustring(), bool inverse = false)
        : inputColorSpace(in)
        , outputColorSpace(out)
        , context_key(key)
        , context_value(val)
        , looks(looks)
        , file(file)
        , namedtransform(namedtransform)
        , inverse(inverse)
    {
        hash = inputColorSpace.hash() + 14033ul * outputColorSpace.hash()
               + 823ul * context_key.hash() + 28411ul * context_value.hash()
               + 1741ul
                     * (looks.hash() + display.hash() + view.hash()
                        + file.hash() + namedtransform.hash())
               + (inverse ? 6421 : 0);
        // N.B. no separate multipliers for looks, display, view, file,
        // namedtransform, because they're never used for the same lookup.
    }

    friend bool operator<(const ColorProcCacheKey& a,
                          const ColorProcCacheKey& b)
    {
        return std::tie(a.hash, a.inputColorSpace, a.outputColorSpace,
                        a.context_key, a.context_value, a.looks, a.display,
                        a.view, a.file, a.namedtransform, a.inverse)
               < std::tie(b.hash, b.inputColorSpace, b.outputColorSpace,
                          b.context_key, b.context_value, b.looks, b.display,
                          b.view, b.file, b.namedtransform, b.inverse);
    }

    friend bool operator==(const ColorProcCacheKey& a,
                           const ColorProcCacheKey& b)
    {
        return std::tie(a.hash, a.inputColorSpace, a.outputColorSpace,
                        a.context_key, a.context_value, a.looks, a.display,
                        a.view, a.file, a.namedtransform, a.inverse)
               == std::tie(b.hash, b.inputColorSpace, b.outputColorSpace,
                           b.context_key, b.context_value, b.looks, b.display,
                           b.view, b.file, b.namedtransform, b.inverse);
    }
    ustring inputColorSpace;
    ustring outputColorSpace;
    ustring context_key;
    ustring context_value;
    ustring looks;
    ustring display;
    ustring view;
    ustring file;
    ustring namedtransform;
    bool inverse;
    size_t hash;
};


struct ColorProcCacheKeyHasher {
    size_t operator()(const ColorProcCacheKey& c) const { return c.hash; }
};


typedef tsl::robin_map<ColorProcCacheKey, ColorProcessorHandle,
                       ColorProcCacheKeyHasher>
    ColorProcessorMap;



bool
ColorConfig::supportsOpenColorIO()
{
    return (disable_ocio == 0);
}



int
ColorConfig::OpenColorIO_version_hex()
{
    return OCIO_VERSION_HEX;
}


struct CSInfo {
    std::string name;  // Name of this color space
    int index;         // More than one can have the same index -- aliases
    enum Flags {
        none               = 0,
        is_linear_response = 1,   // any cs with linear transfer function
        is_scene_linear    = 2,   // equivalent to scene_linear
        is_srgb            = 4,   // sRGB (primaries, and transfer function)
        is_lin_srgb        = 8,   // sRGB/Rec709 primaries, linear response
        is_ACEScg          = 16,  // ACEScg
        is_Rec709          = 32,  // Rec709 primaries and transfer function
        is_known           = is_srgb | is_lin_srgb | is_ACEScg | is_Rec709
    };
    int m_flags   = 0;
    bool examined = false;
    std::string canonical;  // Canonical name for this color space
    OCIO::ConstColorSpaceRcPtr ocio_cs;

    CSInfo(string_view name_, int index_, int flags_ = none,
           string_view canonical_ = "")
        : name(name_)
        , index(index_)
        , m_flags(flags_)
        , canonical(canonical_)
    {
    }

    void setflag(int flagval) { m_flags |= flagval; }

    // Set flag to include any bits in flagval, and also if alias is not yet
    // set, set it to name.
    void setflag(int flagval, std::string& alias)
    {
        m_flags |= flagval;
        if (alias.empty())
            alias = name;
    }

    int flags() const { return m_flags; }
};



// Hidden implementation of ColorConfig
class ColorConfig::Impl {
public:
    OCIO::ConstConfigRcPtr config_;
    OCIO::ConstConfigRcPtr builtinconfig_;
    OCIO::ConstConfigRcPtr interopconfig_;

private:
    std::vector<CSInfo> colorspaces;
    std::string scene_linear_alias;  // Alias for a scene-linear color space
    std::string lin_srgb_alias;
    std::string srgb_alias;
    std::string ACEScg_alias;
    std::string Rec709_alias;
    mutable spin_rw_mutex m_mutex;
    mutable std::string m_error;
    ColorProcessorMap colorprocmap;  // cache of ColorProcessors
    atomic_int colorprocs_requested;
    atomic_int colorprocs_created;
    std::string m_configname;
    std::string m_configfilename;
    ColorConfig* m_self       = nullptr;
    bool m_config_is_built_in = false;

public:
    Impl(ColorConfig* self)
        : m_self(self)
    {
    }

    ~Impl()
    {
#if 0
        // Debugging the cache -- make sure we're creating a small number
        // compared to repeated requests.
        if (colorprocs_requested)
            DBG("ColorConfig::Impl : color procs requested: {}, created: {}\n",
                           colorprocs_requested, colorprocs_created);
#endif
    }

    bool init(string_view filename);

    void add(const std::string& name, int index, int flags = 0)
    {
        spin_rw_write_lock lock(m_mutex);
        colorspaces.emplace_back(name, index, flags);
        // classify(colorspaces.back());
    }

    // Find the CSInfo record for the named color space, or nullptr if it's
    // not a color space we know.
    const CSInfo* find(string_view name) const
    {
        for (auto&& cs : colorspaces)
            if (cs.name == name)
                return &cs;
        return nullptr;
    }
    CSInfo* find(string_view name)
    {
        for (auto&& cs : colorspaces)
            if (cs.name == name)
                return &cs;
        return nullptr;
    }

    // Search for a matching ColorProcessor, return it if found (otherwise
    // return an empty handle).
    ColorProcessorHandle findproc(const ColorProcCacheKey& key)
    {
        ++colorprocs_requested;
        spin_rw_read_lock lock(m_mutex);
        auto found = colorprocmap.find(key);
        return (found == colorprocmap.end()) ? ColorProcessorHandle()
                                             : found->second;
    }

    // Add the given color processor. Be careful -- if a matching one is
    // already in the table, just return the existing one. If they pass
    // in an empty handle, just return it.
    ColorProcessorHandle addproc(const ColorProcCacheKey& key,
                                 ColorProcessorHandle handle)
    {
        if (!handle)
            return handle;
        spin_rw_write_lock lock(m_mutex);
        auto found = colorprocmap.find(key);
        if (found == colorprocmap.end()) {
            // No equivalent item in the map. Add this one.
            colorprocmap[key] = handle;
            ++colorprocs_created;
        } else {
            // There's already an equivalent one. Oops. Discard this one and
            // return the one already in the map.
            handle = found->second;
        }
        return handle;
    }

    int getNumColorSpaces() const { return (int)colorspaces.size(); }

    const char* getColorSpaceNameByIndex(int index) const
    {
        return colorspaces[index].name.c_str();
    }

    string_view resolve(string_view name) const;

    // Note: Uses std::format syntax
    template<typename... Args>
    void error(const char* fmt, const Args&... args) const
    {
        spin_rw_write_lock lock(m_mutex);
        m_error = Strutil::fmt::format(fmt, args...);
    }
    std::string geterror(bool clear = true) const
    {
        std::string err;
        spin_rw_write_lock lock(m_mutex);
        if (clear) {
            std::swap(err, m_error);
        } else {
            err = m_error;
        }
        return err;
    }
    bool haserror() const
    {
        spin_rw_read_lock lock(m_mutex);
        return !m_error.empty();
    }
    void clear_error()
    {
        spin_rw_write_lock lock(m_mutex);
        m_error.clear();
    }

    std::vector<string_view> get_builtin_interop_ids() const;
    static OCIO::ConstConfigRcPtr build_interop_identities_config();

    const std::string& configfilename() const { return m_configfilename; }
    void configfilename(string_view filename) { m_configfilename = filename; }

    const std::string& configname() const { return m_configname; }
    void configname(string_view name) { m_configname = name; }

    OCIO::ConstCPUProcessorRcPtr
    get_to_builtin_cpu_proc(const char* my_from, const char* builtin_to) const;

    bool isColorSpaceLinear(string_view name) const;

private:
    // Return the CSInfo flags for the given color space name
    int flags(string_view name)
    {
        CSInfo* cs = find(name);
        if (!cs)
            return 0;
        examine(cs);
        spin_rw_read_lock lock(m_mutex);
        return cs->flags();
    }

    // Set cs.flag to include any bits in flagval.
    void setflag(CSInfo& cs, int flagval)
    {
        spin_rw_write_lock lock(m_mutex);
        cs.setflag(flagval);
    }

    // Set cs.flag to include any bits in flagval, and also if alias is not
    // yet set, set it to cs.name.
    void setflag(CSInfo& cs, int flagval, std::string& alias)
    {
        spin_rw_write_lock lock(m_mutex);
        cs.setflag(flagval, alias);
    }

    void inventory();

    // Set the flags for the given color space and canonical name, if we can
    // make a guess based on the name. This is very inexpensive. This should
    // only be called from within a lock of the mutex.
    void classify_by_name(CSInfo& cs);

    // Set the flags for the given color space and canonical name, trying some
    // tricks to deduce the color space from the primaries, white point, and
    // transfer function. This is more expensive, and might only work for OCIO
    // 2.2 and above. This should only be called from within a lock of the
    // mutex.
    void classify_by_conversions(CSInfo& cs);

    // Apply more heuristics to try to deduce more color space information.
    void reclassify_heuristics(CSInfo& cs);

    // If the CSInfo hasn't yet been "examined" (fully classified by all
    // heuristics), do so. This should NOT be called from within a lock of the
    // mutex.
    void examine(CSInfo* cs)
    {
        if (!cs->examined) {
            spin_rw_write_lock lock(m_mutex);
            if (!cs->examined) {
                classify_by_name(*cs);
                classify_by_conversions(*cs);
                reclassify_heuristics(*cs);
                cs->examined = true;
            }
        }
    }

    void debug_print_aliases()
    {
        DBG("Aliases: scene_linear={}   lin_srgb={}   srgb={}   ACEScg={}   Rec709={}\n",
            scene_linear_alias, lin_srgb_alias, srgb_alias, ACEScg_alias,
            Rec709_alias);
    }

    // For OCIO 2.3+, we can ask for the equivalent of some built-in
    // color spaces.
    void identify_builtin_equivalents();

    bool check_same_as_builtin_transform(const char* my_from,
                                         const char* builtin_to) const;
    bool test_conversion_yields(const char* from, const char* to,
                                cspan<Imath::C3f> test_colors,
                                cspan<Imath::C3f> result_colors) const;
    const char* IdentifyBuiltinColorSpace(const char* name) const;
};



// ColorConfig utility to take inventory of the color spaces available.
// It sets up knowledge of "linear", "srgb_rec709_scene", "Rec709", etc,
// even if the underlying OCIO configuration lacks them.
void
ColorConfig::Impl::inventory()
{
    DBG("inventorying config {}\n", configname());
    if (config_ && !disable_ocio) {
        bool nonraw = false;
        for (int i = 0, e = config_->getNumColorSpaces(); i < e; ++i) {
            auto csname = config_->getColorSpaceNameByIndex(i);
            auto cs     = (csname) ? config_->getColorSpace(c_str(csname))
                                   : nullptr;
            nonraw |= !cs->isData();  // prevent unused variable warning
        }
        if (nonraw) {
            for (int i = 0, e = config_->getNumColorSpaces(); i < e; ++i)
                add(config_->getColorSpaceNameByIndex(i), i);
            for (auto&& cs : colorspaces)
                classify_by_name(cs);
            OCIO::ConstColorSpaceRcPtr lin = config_->getColorSpace(
                "scene_linear");
            if (lin)
                scene_linear_alias = lin->getName();
            return;  // If any non-"raw" spaces were defined, we're done
        }
    }
    // If we had some kind of bogus configuration that seemed to define
    // only a "raw" color space and nothing else, that's useless, so
    // figure out our own way to move forward.
    config_.reset();

    // If there was no configuration, or we didn't compile with OCIO
    // support at all, register a few basic names we know about.
    // For the "no OCIO / no config" case, we assume an unsophisticated
    // color pipeline where "linear" and the like are all assumed to use
    // Rec709/sRGB color primaries.
    int linflags = CSInfo::is_linear_response | CSInfo::is_scene_linear
                   | CSInfo::is_lin_srgb;
    add("linear", 0, linflags);
    add("scene_linear", 0, linflags);
    add("default", 0, linflags);
    add("rgb", 0, linflags);
    add("RGB", 0, linflags);
    add("lin_rec709_scene", 0, linflags);
    add("lin_srgb", 0, linflags);
    add("lin_rec709", 0, linflags);
    add("srgb_rec709_scene", 1, CSInfo::is_srgb);
    add("sRGB", 1, CSInfo::is_srgb);
    add("Rec709", 2, CSInfo::is_Rec709);

    for (auto&& cs : colorspaces)
        classify_by_name(cs);
}



inline bool
close_colors(cspan<Imath::C3f> a, cspan<Imath::C3f> b)
{
    OIIO_DASSERT(a.size() == b.size());
    for (size_t i = 0, e = a.size(); i < e; ++i)
        if (std::abs(a[i].x - b[i].x) > 1.0e-3f
            || std::abs(a[i].y - b[i].y) > 1.0e-3f
            || std::abs(a[i].z - b[i].z) > 1.0e-3f)
            return false;
    return true;
}



OCIO::ConstCPUProcessorRcPtr
ColorConfig::Impl::get_to_builtin_cpu_proc(const char* my_from,
                                           const char* builtin_to) const
{
    try {
        auto proc = OCIO::Config::GetProcessorToBuiltinColorSpace(config_,
                                                                  my_from,
                                                                  builtin_to);
        return proc ? proc->getDefaultCPUProcessor()
                    : OCIO::ConstCPUProcessorRcPtr();
    } catch (...) {
        return {};
    }
}



// Is this config's `my_from` color space equivalent to the built-in
// `builtin_to` color space? Find out by transforming the primaries, white,
// and half white and see if the results indicate that it was the identity
// transform (or close enough).
bool
ColorConfig::Impl::check_same_as_builtin_transform(const char* my_from,
                                                   const char* builtin_to) const
{
    if (disable_builtin_configs)
        return false;
    auto proc = get_to_builtin_cpu_proc(my_from, builtin_to);
    if (proc) {
        Imath::C3f colors[n_test_colors];
        std::copy(test_colors, test_colors + n_test_colors, colors);
        proc->apply(OCIO::PackedImageDesc(colors, n_test_colors, 1, 3));
        if (close_colors(colors, test_colors))
            return true;
    }
    return false;
}



// If we transform test_colors from "from" to "to" space, do we get
// result_colors? This is a building block for deducing some color spaces.
bool
ColorConfig::Impl::test_conversion_yields(const char* from, const char* to,
                                          cspan<Imath::C3f> test_colors,
                                          cspan<Imath::C3f> result_colors) const
{
    auto proc = m_self->createColorProcessor(from, to);
    if (!proc)
        return false;
    OIIO_DASSERT(test_colors.size() == result_colors.size());
    auto n             = test_colors.size();
    Imath::C3f* colors = OIIO_ALLOCA(Imath::C3f, n);
    std::copy(test_colors.data(), test_colors.data() + n, colors);
    proc->apply((float*)colors, int(n), 1, 3, sizeof(float), 3 * sizeof(float),
                int(n) * 3 * sizeof(float));
    return close_colors({ colors, n }, result_colors);
}



static bool
transform_has_Lut3D(string_view name, OCIO::ConstTransformRcPtr transform,
                    OCIO::ConstConfigRcPtr config = nullptr)
{
    using namespace OCIO;
    auto ttype = transform ? transform->getTransformType() : -1;
    if (ttype == TRANSFORM_TYPE_LUT3D || ttype == TRANSFORM_TYPE_LOOK
        || ttype == TRANSFORM_TYPE_DISPLAY_VIEW) {
        return true;
    }
    if (ttype == TRANSFORM_TYPE_FILE) {
        // If the filename ends in ".spi1d" or ".spimtx", it's not a 3D LUT.
        auto filetransform = dynamic_cast<const FileTransform*>(
            transform.get());
        std::string src = filetransform->getSrc();
        Strutil::to_lower(src);
        if (!Strutil::ends_with(src, ".spi1d")
            && !Strutil::ends_with(src, ".spimtx"))
            return true;
    }
    if (ttype == TRANSFORM_TYPE_GROUP) {
        auto group = dynamic_cast<const GroupTransform*>(transform.get());
        for (int i = 0, n = group->getNumTransforms(); i < n; ++i) {
            if (transform_has_Lut3D(group->getFormatMetadata().getName(),
                                    group->getTransform(i), config))
                return true;
        }
    }
    if (ttype == TRANSFORM_TYPE_COLORSPACE) {
        if (!config)
            return false;

        auto cs_transform = dynamic_cast<const ColorSpaceTransform*>(
            transform.get());
        auto src = cs_transform->getSrc();
        auto dst = cs_transform->getDst();
        // Collect the transforms for source and destination color spaces

        if (!src && !dst)  // is reference space
            return false;

        if (!src || !dst) {  // is named transform
            auto nt = (src) ? config->getNamedTransform(c_str(src))
                            : config->getNamedTransform(c_str(dst));
            if (nt) {
                auto fwd_xform = nt->getTransform(TRANSFORM_DIR_FORWARD);
                if (fwd_xform
                    && transform_has_Lut3D(nt->getName(), fwd_xform, config))
                    return true;
            }
        }

        // collect source color space transforms
        auto src_cs       = config->getColorSpace(c_str(src));
        auto dst_cs       = config->getColorSpace(c_str(dst));
        auto src_to_ref   = src_cs->getTransform(COLORSPACE_DIR_TO_REFERENCE);
        auto src_from_ref = src_cs->getTransform(COLORSPACE_DIR_FROM_REFERENCE);
        auto dst_to_ref   = dst_cs->getTransform(COLORSPACE_DIR_TO_REFERENCE);
        auto dst_from_ref = dst_cs->getTransform(COLORSPACE_DIR_FROM_REFERENCE);
        if (src_to_ref)
            if (transform_has_Lut3D(name, src_to_ref, config))
                return true;
        if (dst_to_ref)
            if (transform_has_Lut3D(name, dst_to_ref, config))
                return true;
        if (src_from_ref)
            if (transform_has_Lut3D(name, src_from_ref, config))
                return true;
        if (dst_from_ref)
            if (transform_has_Lut3D(name, dst_from_ref, config))
                return true;
    }
    if (name.size() && ttype >= 0)
        DBG("{} has type {}\n", name, ttype);
    return false;
}



void
ColorConfig::Impl::classify_by_name(CSInfo& cs)
{
    // General heuristics based on the names -- for a few canonical names,
    // believe them! Woe be unto the poor soul who names a color space "sRGB"
    // or "ACEScg" and it's really something entirely different.
    if (Strutil::iequals(cs.name, "srgb_rec709_scene")
        || Strutil::iequals(cs.name, "srgb_tx")
        || Strutil::iequals(cs.name, "srgb_texture")
        || Strutil::iequals(cs.name, "srgb texture")
        || Strutil::iequals(cs.name, "srgb_rec709_scene")
        || Strutil::iequals(cs.name, "sRGB - Texture")
        || Strutil::iequals(cs.name, "sRGB")) {
        cs.setflag(CSInfo::is_srgb, srgb_alias);
    } else if (Strutil::iequals(cs.name, "lin_rec709_scene")
               || Strutil::iequals(cs.name, "lin_rec709")
               || Strutil::iequals(cs.name, "Linear Rec.709 (sRGB)")
               || Strutil::iequals(cs.name, "lin_srgb")
               || Strutil::iequals(cs.name, "linear")) {
        cs.setflag(CSInfo::is_lin_srgb | CSInfo::is_linear_response,
                   lin_srgb_alias);
    } else if (Strutil::iequals(cs.name, "ACEScg")
               || Strutil::iequals(cs.name, "lin_ap1_scene")
               || Strutil::iequals(cs.name, "lin_ap1")) {
        cs.setflag(CSInfo::is_ACEScg | CSInfo::is_linear_response,
                   ACEScg_alias);
    } else if (Strutil::iequals(cs.name, "Rec709")) {
        cs.setflag(CSInfo::is_Rec709, Rec709_alias);
    }
#ifdef OIIO_SITE_spi
    // Ugly SPI-specific hacks, so sorry
    else if (Strutil::starts_with(cs.name, "cgln")) {
        cs.setflag(CSInfo::is_ACEScg | CSInfo::is_linear_response,
                   ACEScg_alias);
    } else if (cs.name == "srgbf" || cs.name == "srgbh" || cs.name == "srgb16"
               || cs.name == "srgb8") {
        cs.setflag(CSInfo::is_srgb, srgb_alias);
    } else if (cs.name == "srgblnf" || cs.name == "srgblnh"
               || cs.name == "srgbln16" || cs.name == "srgbln8") {
        cs.setflag(CSInfo::is_lin_srgb, lin_srgb_alias);
    }
#endif

    // Set up some canonical names
    if (cs.flags() & CSInfo::is_srgb)
        cs.canonical = "srgb_rec709_scene";
    else if (cs.flags() & CSInfo::is_lin_srgb)
        cs.canonical = "lin_rec709_scene";
    else if (cs.flags() & CSInfo::is_ACEScg)
        cs.canonical = "lin_ap1_scene";
    else if (cs.flags() & CSInfo::is_Rec709)
        cs.canonical = "Rec709";
    if (cs.canonical.size()) {
        DBG("classify by name identified '{}' as canonical {}\n", cs.name,
            cs.canonical);
        cs.examined = true;
    }
}



void
ColorConfig::Impl::classify_by_conversions(CSInfo& cs)
{
    DBG("classifying by conversions {}\n", cs.name);
    if (cs.examined)
        return;  // Already classified

    if (isColorSpaceLinear(cs.name))
        cs.setflag(CSInfo::is_linear_response);

    // If the name didn't already tell us what it is, and we have a new enough
    // OCIO that has built-in configs, test whether this color space is
    // equivalent to one of a few particular built-in color spaces. That lets
    // us identify some color spaces even if they are named something
    // nonstandard. Skip this part if the color space we're classifying is
    // itself part of the built-in config -- in that case, it will already be
    // tagged correctly by the name above.
    if (!(cs.flags() & CSInfo::is_known) && config_ && !disable_ocio
        && !m_config_is_built_in) {
        using namespace OCIO;
        cs.ocio_cs = config_->getColorSpace(cs.name.c_str());
        if (transform_has_Lut3D(
                cs.name, cs.ocio_cs->getTransform(COLORSPACE_DIR_TO_REFERENCE),
                config_)
            || transform_has_Lut3D(cs.name,
                                   cs.ocio_cs->getTransform(
                                       COLORSPACE_DIR_FROM_REFERENCE),
                                   config_)) {
            // Skip things with LUT3d because they are expensive due to LUT
            // inversion costs, and they're not gonna be our favourite
            // canonical spaces anyway.
            // DBG("{} has LUT3\n", cs.name);
        } else if (check_same_as_builtin_transform(cs.name.c_str(), "srgb_tx")) {
            cs.setflag(CSInfo::is_srgb_rec709, srgb_alias);
        } else if (check_same_as_builtin_transform(cs.name.c_str(),
                                                   "lin_srgb")) {
            cs.setflag(CSInfo::is_lin_srgb | CSInfo::is_linear_response,
                       lin_srgb_alias);
        } else if (check_same_as_builtin_transform(cs.name.c_str(), "ACEScg")) {
            cs.setflag(CSInfo::is_ACEScg | CSInfo::is_linear_response,
                       ACEScg_alias);
        }
    }

    // Set up some canonical names
    if (cs.flags() & CSInfo::is_srgb)
        cs.canonical = "srgb_rec709_scene";
    else if (cs.flags() & CSInfo::is_lin_srgb)
        cs.canonical = "lin_rec709_scene";
    else if (cs.flags() & CSInfo::is_ACEScg)
        cs.canonical = "lin_ap1_scene";
    else if (cs.flags() & CSInfo::is_Rec709)
        cs.canonical = "Rec709";
}



void
ColorConfig::Impl::reclassify_heuristics(CSInfo& cs)
{
#if OCIO_VERSION_HEX < MAKE_OCIO_VERSION_HEX(2, 2, 0)
    // Extra checks for OCIO < 2.2. For >= 2.2, there is no need, we
    // already figured this out using the built-in configs.
    if (!(cs.flags() & CSInfo::is_known)) {
        // If this isn't one of the known color spaces, let's try some
        // tricks!
        static float srgb05 = linear_to_sRGB(0.5f);
        static Imath::C3f lin_srgb_to_srgb_results[n_test_colors]
            = { { 1, 0, 0 },
                { 0, 1, 0 },
                { 0, 0, 1 },
                { 1, 1, 1 },
                { srgb05, srgb05, srgb05 } };
        // If there is a known srgb space, and transforming our test
        // colors from "this cs" to srgb gives us what we expect for a
        // lin_srgb->srgb, then guess what? -- this is lin_srgb!
        if (srgb_alias.size()
            && test_conversion_yields(cs.name.c_str(), srgb_alias.c_str(),
                                      test_colors, lin_srgb_to_srgb_results)) {
            setflag(cs, CSInfo::is_lin_srgb | CSInfo::is_linear_response,
                    lin_srgb_alias);
            cs.canonical = "lin_srgb";
        }
    }
#endif
}



void
ColorConfig::Impl::identify_builtin_equivalents()
{
    if (disable_builtin_configs)
        return;
    Timer timer;
    if (auto n = IdentifyBuiltinColorSpace("srgb_tx")) {
        if (CSInfo* cs = find(n)) {
            cs->setflag(CSInfo::is_srgb, srgb_alias);
            DBG("Identified {} = builtin '{}'\n", "srgb_rec709_scene",
                cs->name);
        }
    } else {
        DBG("No config space identified as srgb\n");
    }
    DBG("identify_builtin_equivalents srgb took {:0.2f}s\n", timer.lap());
    if (auto n = IdentifyBuiltinColorSpace("lin_srgb")) {
        if (CSInfo* cs = find(n)) {
            cs->setflag(CSInfo::is_lin_srgb | CSInfo::is_linear_response,
                        lin_srgb_alias);
            DBG("Identified {} = builtin '{}'\n", "lin_rec709_scene", cs->name);
        }
    } else {
        DBG("No config space identified as lin_srgb\n");
    }
    DBG("identify_builtin_equivalents lin_srgb took {:0.2f}s\n", timer.lap());
    if (auto n = IdentifyBuiltinColorSpace("ACEScg")) {
        if (CSInfo* cs = find(n)) {
            cs->setflag(CSInfo::is_ACEScg | CSInfo::is_linear_response,
                        ACEScg_alias);
            DBG("Identified {} = builtin '{}'\n", "ACEScg", cs->name);
        }
    } else {
        DBG("No config space identified as acescg\n");
    }
    DBG("identify_builtin_equivalents acescg took {:0.2f}s\n", timer.lap());
}



const char*
ColorConfig::Impl::IdentifyBuiltinColorSpace(const char* name) const
{
    if (!config_ || disable_builtin_configs)
        return nullptr;
    try {
        return OCIO::Config::IdentifyBuiltinColorSpace(config_, interopconfig_,
                                                       name);
    } catch (...) {
    }
    try {
        return OCIO::Config::IdentifyBuiltinColorSpace(config_, builtinconfig_,
                                                       name);
    } catch (...) {
    }
    return nullptr;
}



ColorConfig::ColorConfig(string_view filename) { reset(filename); }



ColorConfig::~ColorConfig() {}



bool
ColorConfig::Impl::init(string_view filename)
{
    OIIO_MAYBE_UNUSED Timer timer;
    bool ok = true;

    auto oldlog = OCIO::GetLoggingLevel();
    OCIO::SetLoggingLevel(OCIO::LOGGING_LEVEL_NONE);

    try {
        builtinconfig_ = OCIO::Config::CreateFromFile("ocio://default");
    } catch (OCIO::Exception& e) {
        error("Error making OCIO built-in config: {}", e.what());
    }

    // create builtin interop identities config
    try {
        interopconfig_ = build_interop_identities_config();
    } catch (OCIO::Exception& e) {
        error("Error making OCIO interop identities config: {}", e.what());
    }

    // If no filename was specified, use env $OCIO
    if (filename.empty() || Strutil::iequals(filename, "$OCIO"))
        filename = Sysutil::getenv("OCIO");
    if (filename.empty())
        filename = "ocio://default";
    // If there's a newline in filename, treat it as the config data itself
    if (filename.find('\n') != string_view::npos) {
        try {
            std::istringstream iss;
            iss.str(std::string(filename));
            config_ = OCIO::Config::CreateFromStream(iss);
            configname(
                filename);  // TODO: rename 'configname'function to 'configuri'?
            configfilename("");  // from stream, no filename
        } catch (OCIO::Exception& e) {
            error("Error reading OCIO config from stream: {}", e.what());
        }
    }
    if (filename.size() && !OIIO::Filesystem::exists(filename)
        && !Strutil::istarts_with(filename, "ocio://")) {
        error("Requested non-existent OCIO config \"{}\"", filename);
    } else {
        // Either filename passed, or taken from $OCIO, and it seems to exist
        try {
            config_ = OCIO::Config::CreateFromFile(
                std::string(filename).c_str());
            configname(filename);
            configfilename(filename);
            m_config_is_built_in = Strutil::istarts_with(filename, "ocio://");
        } catch (OCIO::Exception& e) {
            error("Error reading OCIO config \"{}\": {}", filename, e.what());
        }
    }

    if (!config_) {
        OCIO::LogMessage(OCIO::LOGGING_LEVEL_DEBUG,
                         "Falling back to current OCIO config");
        auto current_config = OCIO::GetCurrentConfig();
        if (current_config->getNumColorSpaces() == 1) {
            config_ = OCIO::Config::CreateFromFile("ocio://default");
            configname("ocio://default");
            configfilename("ocio://default");
            OCIO::LogMessage(
                OCIO::LOGGING_LEVEL_DEBUG,
                "Current OCIO config is invalid, using ocio://default "
                "instead");
            OCIO::SetCurrentConfig(config_);
        } else
            config_ = current_config;
        auto config_name = current_config->getName();
        configname(config_name ? config_name : "current");
        configfilename("current");
    }

    OCIO::SetLoggingLevel(oldlog);

    ok = config_.get() != nullptr;

    DBG("OCIO config {} loaded in {:0.2f} seconds\n", filename, timer.lap());

    inventory();
    // NOTE: inventory already does classify_by_name

    DBG("\nIDENTIFY BUILTIN EQUIVALENTS\n");
    identify_builtin_equivalents();  // OCIO 2.3+ only
    DBG("OCIO 2.3+ builtin equivalents in {:0.2f} seconds\n", timer.lap());

#if 1
    for (auto&& cs : colorspaces) {
        // examine(&cs);
        DBG("Color space '{}':\n", cs.name);
        if (cs.flags() & CSInfo::is_srgb)
            DBG("'{}' is srgb\n", cs.name);
        if (cs.flags() & CSInfo::is_lin_srgb)
            DBG("'{}' is lin_srgb\n", cs.name);
        if (cs.flags() & CSInfo::is_ACEScg)
            DBG("'{}' is ACEScg\n", cs.name);
        if (cs.flags() & CSInfo::is_Rec709)
            DBG("'{}' is Rec709\n", cs.name);
        if (cs.flags() & CSInfo::is_linear_response)
            DBG("'{}' has linear response\n", cs.name);
        if (cs.flags() & CSInfo::is_scene_linear)
            DBG("'{}' is scene_linear\n", cs.name);
        if (cs.flags())
            DBG("\n");
    }
#endif
    debug_print_aliases();
    DBG("OCIO config {} classified in {:0.2f} seconds\n", filename,
        timer.lap());

    return ok;
}



bool
ColorConfig::reset(string_view filename)
{
    OIIO::pvt::LoggedTimer logtime("ColorConfig::reset");
    if (m_impl
        && (filename == getImpl()->configname()
            || (filename == ""
                && getImpl()->configname() == "ocio://default"))) {
        // Request to reset to the config we're already using. Just return,
        // don't do anything expensive.
        return true;
    }

    m_impl.reset(new ColorConfig::Impl(this));
    return m_impl->init(filename);
}



bool
ColorConfig::has_error() const
{
    return (getImpl()->haserror());
}



std::string
ColorConfig::geterror(bool clear) const
{
    return getImpl()->geterror(clear);
}



int
ColorConfig::getNumColorSpaces() const
{
    return (int)getImpl()->getNumColorSpaces();
}



const char*
ColorConfig::getColorSpaceNameByIndex(int index) const
{
    return getImpl()->getColorSpaceNameByIndex(index);
}



int
ColorConfig::getColorSpaceIndex(string_view name) const
{
    // Check for exact matches
    for (int i = 0, e = getNumColorSpaces(); i < e; ++i)
        if (Strutil::iequals(getColorSpaceNameByIndex(i), name))
            return i;
    // Check for aliases and equivalents
    for (int i = 0, e = getNumColorSpaces(); i < e; ++i)
        if (equivalent(getColorSpaceNameByIndex(i), name))
            return i;
    return -1;
}



const char*
ColorConfig::getColorSpaceFamilyByName(string_view name) const
{
    if (getImpl()->config_ && !disable_ocio) {
        OCIO::ConstColorSpaceRcPtr c = getImpl()->config_->getColorSpace(
            std::string(name).c_str());
        if (c)
            return c->getFamily();
    }
    return nullptr;
}



std::vector<std::string>
ColorConfig::getColorSpaceNames() const
{
    std::vector<std::string> result;
    int n = getNumColorSpaces();
    result.reserve(n);
    for (int i = 0; i < n; ++i)
        result.emplace_back(getColorSpaceNameByIndex(i));
    return result;
}

int
ColorConfig::getNumRoles() const
{
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->config_->getNumRoles();
    return 0;
}

const char*
ColorConfig::getRoleByIndex(int index) const
{
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->config_->getRoleName(index);
    return nullptr;
}


std::vector<std::string>
ColorConfig::getRoles() const
{
    std::vector<std::string> result;
    for (int i = 0, e = getNumRoles(); i != e; ++i)
        result.emplace_back(getRoleByIndex(i));
    return result;
}



int
ColorConfig::getNumLooks() const
{
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->config_->getNumLooks();
    return 0;
}



const char*
ColorConfig::getLookNameByIndex(int index) const
{
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->config_->getLookNameByIndex(index);
    return nullptr;
}



std::vector<std::string>
ColorConfig::getLookNames() const
{
    std::vector<std::string> result;
    for (int i = 0, e = getNumLooks(); i != e; ++i)
        result.emplace_back(getLookNameByIndex(i));
    return result;
}



bool
ColorConfig::isColorSpaceLinear(string_view name) const
{
    return getImpl()->isColorSpaceLinear(name);
}



bool
ColorConfig::Impl::isColorSpaceLinear(string_view name) const
{
    if (config_ && !disable_builtin_configs && !disable_ocio) {
        try {
            return config_->isColorSpaceLinear(c_str(name),
                                               OCIO::REFERENCE_SPACE_SCENE)
                   || config_->isColorSpaceLinear(c_str(name),
                                                  OCIO::REFERENCE_SPACE_DISPLAY);
        } catch (const std::exception& e) {
            error("ColorConfig error: {}", e.what());
            return false;
        }
    }
    return Strutil::iequals(name, "linear")
           || Strutil::istarts_with(name, "linear ")
           || Strutil::istarts_with(name, "linear_")
           || Strutil::istarts_with(name, "lin_")
           || Strutil::iends_with(name, "_linear")
           || Strutil::iends_with(name, "_lin");
}



std::vector<std::string>
ColorConfig::getAliases(string_view color_space) const
{
    std::vector<std::string> result;
    auto config = getImpl()->config_;
    if (config) {
        auto cs = config->getColorSpace(c_str(color_space));
        if (cs) {
            for (int i = 0, e = cs->getNumAliases(); i < e; ++i)
                result.emplace_back(cs->getAlias(i));
        }
    }
    return result;
}



const char*
ColorConfig::getColorSpaceNameByRole(string_view role) const
{
    if (getImpl()->config_ && !disable_ocio) {
        using Strutil::print;
        OCIO::ConstColorSpaceRcPtr c = getImpl()->config_->getColorSpace(
            std::string(role).c_str());
        // DBG("looking first for named color space {} -> {}\n", role,
        //     c ? c->getName() : "not found");
        // Catch special case of obvious name synonyms
        if (!c
            && (Strutil::iequals(role, "RGB")
                || Strutil::iequals(role, "default")))
            role = string_view("linear");
        if (!c && Strutil::iequals(role, "linear"))
            c = getImpl()->config_->getColorSpace("scene_linear");
        if (!c && Strutil::iequals(role, "scene_linear"))
            c = getImpl()->config_->getColorSpace("linear");
        if (!c && Strutil::iequals(role, "srgb")) {
            c = getImpl()->config_->getColorSpace("sRGB - Texture");
            // DBG("Unilaterally substituting {} -> '{}'\n", role,
            //                c->getName());
        }

        if (c) {
            // DBG("found color space {} for role {}\n", c->getName(),
            //                role);
            return c->getName();
        }
    }

    // No OCIO at build time, or no OCIO configuration at run time
    if (Strutil::iequals(role, "linear")
        || Strutil::iequals(role, "scene_linear"))
        return "linear";

    return NULL;  // Dunno what role
}



TypeDesc
ColorConfig::getColorSpaceDataType(string_view name, int* bits) const
{
    if (getImpl()->config_ && !disable_ocio) {
        OCIO::ConstColorSpaceRcPtr c = getImpl()->config_->getColorSpace(
            std::string(name).c_str());
        if (c) {
            OCIO::BitDepth b = c->getBitDepth();
            switch (b) {
            case OCIO::BIT_DEPTH_UNKNOWN: return TypeDesc::UNKNOWN;
            case OCIO::BIT_DEPTH_UINT8: *bits = 8; return TypeDesc::UINT8;
            case OCIO::BIT_DEPTH_UINT10: *bits = 10; return TypeDesc::UINT16;
            case OCIO::BIT_DEPTH_UINT12: *bits = 12; return TypeDesc::UINT16;
            case OCIO::BIT_DEPTH_UINT14: *bits = 14; return TypeDesc::UINT16;
            case OCIO::BIT_DEPTH_UINT16: *bits = 16; return TypeDesc::UINT16;
            case OCIO::BIT_DEPTH_UINT32: *bits = 32; return TypeDesc::UINT32;
            case OCIO::BIT_DEPTH_F16: *bits = 16; return TypeDesc::HALF;
            case OCIO::BIT_DEPTH_F32: *bits = 32; return TypeDesc::FLOAT;
            }
        }
    }
    return TypeUnknown;
}



int
ColorConfig::getNumDisplays() const
{
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->config_->getNumDisplays();
    return 0;
}



const char*
ColorConfig::getDisplayNameByIndex(int index) const
{
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->config_->getDisplay(index);
    return nullptr;
}



std::vector<std::string>
ColorConfig::getDisplayNames() const
{
    std::vector<std::string> result;
    for (int i = 0, e = getNumDisplays(); i != e; ++i)
        result.emplace_back(getDisplayNameByIndex(i));
    return result;
}



int
ColorConfig::getNumViews(string_view display) const
{
    if (display.empty())
        display = getDefaultDisplayName();
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->config_->getNumViews(std::string(display).c_str());
    return 0;
}



const char*
ColorConfig::getViewNameByIndex(string_view display, int index) const
{
    if (display.empty())
        display = getDefaultDisplayName();
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->config_->getView(std::string(display).c_str(), index);
    return nullptr;
}



std::vector<std::string>
ColorConfig::getViewNames(string_view display) const
{
    std::vector<std::string> result;
    if (display.empty())
        display = getDefaultDisplayName();
    for (int i = 0, e = getNumViews(display); i != e; ++i)
        result.emplace_back(getViewNameByIndex(display, i));
    return result;
}



const char*
ColorConfig::getDefaultDisplayName() const
{
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->config_->getDefaultDisplay();
    return nullptr;
}



const char*
ColorConfig::getDefaultViewName(string_view display) const
{
    if (display.empty() || display == "default")
        display = getDefaultDisplayName();
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->config_->getDefaultView(c_str(display));
    return nullptr;
}


const char*
ColorConfig::getDefaultViewName(string_view display,
                                string_view inputColorSpace) const
{
    if (display.empty() || display == "default")
        display = getDefaultDisplayName();
    if (inputColorSpace.empty() || inputColorSpace == "default")
        inputColorSpace = getImpl()->config_->getColorSpaceFromFilepath(
            c_str(inputColorSpace));
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->config_->getDefaultView(c_str(display),
                                                  c_str(inputColorSpace));
    return nullptr;
}


const char*
ColorConfig::getDisplayViewColorSpaceName(const std::string& display,
                                          const std::string& view) const
{
    if (getImpl()->config_ && !disable_ocio) {
        string_view name
            = getImpl()->config_->getDisplayViewColorSpaceName(c_str(display),
                                                               c_str(view));
        // Handle certain Shared View cases
        if (strcmp(c_str(name), "<USE_DISPLAY_NAME>") == 0)
            name = display;
        return c_str(name);
    }
    return nullptr;
}



const char*
ColorConfig::getDisplayViewLooks(const std::string& display,
                                 const std::string& view) const
{
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->config_->getDisplayViewLooks(display.c_str(),
                                                       view.c_str());
    return nullptr;
}



int
ColorConfig::getNumNamedTransforms() const
{
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->config_->getNumNamedTransforms();
    return 0;
}



const char*
ColorConfig::getNamedTransformNameByIndex(int index) const
{
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->config_->getNamedTransformNameByIndex(index);
    return nullptr;
}



std::vector<std::string>
ColorConfig::getNamedTransformNames() const
{
    std::vector<std::string> result;
    for (int i = 0, e = getNumNamedTransforms(); i != e; ++i)
        result.emplace_back(getNamedTransformNameByIndex(i));
    return result;
}



std::vector<std::string>
ColorConfig::getNamedTransformAliases(string_view named_transform) const
{
    std::vector<std::string> result;
    auto config = getImpl()->config_;
    if (config) {
        auto nt = config->getNamedTransform(c_str(named_transform));
        if (nt) {
            for (int i = 0, e = nt->getNumAliases(); i < e; ++i)
                result.emplace_back(nt->getAlias(i));
        }
    }
    return result;
}



std::string
ColorConfig::configname() const
{
    if (getImpl()->config_ && !disable_ocio)
        return getImpl()->configname();
    return "built-in";
}



std::string
ColorConfig::configfilename() const
{
    return getImpl()->configfilename();
}



std::string
ColorConfig::ocioconfigname() const
{
    return getImpl()->config_->getName();
}



string_view
ColorConfig::resolve(string_view name) const
{
    return getImpl()->resolve(name);
}



OCIO::ConstConfigRcPtr
ColorConfig::Impl::build_interop_identities_config()
{
    std::istringstream iss(kInteropIdentitiesConfig);
    auto oiio_interop_identities = OCIO::Config::CreateFromStream(iss);

#if OCIO_VERSION_HEX >= MAKE_OCIO_VERSION_HEX(2, 5, 0)
    // Start with the latest studio config as the base
    auto studio_config = OCIO::Config::CreateFromFile(
        "ocio://studio-config-latest");
    auto studio_config_identities = studio_config->createEditableCopy();
    // Make all color spaces visible and give the config a special name
    studio_config_identities->setInactiveColorSpaces("");
    studio_config_identities->setName("oiio:interop-identities");
    // Set each color space name to its interop ID, adding the old name
    // as an alias. We can do this fearlessly as long as each color
    // space in the OCIO-2.5+ builtin configs have a unique interop_id.
    std::vector<std::string> names;
    for (int i = 0, e = studio_config_identities->getNumColorSpaces(); i < e;
         ++i)
        names.emplace_back(
            studio_config_identities->getColorSpaceNameByIndex(i));
    for (const auto& n : names) {
        auto cs = studio_config_identities->getColorSpace(n.c_str());
        if (!cs)
            continue;
        const char* interop_id = cs->getInteropID();
        if (!interop_id || !*interop_id)
            continue;
        std::string old = cs->getName();
        if (old == interop_id)
            continue;
        cs->setName(interop_id);
        cs->addAlias(old.c_str());
    }
    studio_config_identities->validate();

    // Merge any missing color spaces from OIIO's builtin interop config into
    // the studio config. The OIIO builtin interop config is deliberately
    // constructed such that its color spaces can be dropped into the studio
    // config with as little effort as possible.
    // TODO: Use the config merging tools provided by OCIO-2.5+.
    std::vector<std::string> oiio_interop_ids;
    for (int i = 0, e = oiio_interop_identities->getNumColorSpaces(); i < e;
         ++i)
        oiio_interop_ids.emplace_back(
            oiio_interop_identities->getColorSpaceNameByIndex(i));
    for (const auto& n : oiio_interop_ids) {
        if (studio_config_identities->getColorSpace(n.c_str()))
            continue;  // already present
        OCIO::ConstColorSpaceRcPtr cs = oiio_interop_identities->getColorSpace(
            n.c_str());
        try {
            studio_config_identities->addColorSpace(cs);
        } catch (OCIO::Exception& e) {
            // Name collision -- remove aliases and try again
            try {
                OCIO::ColorSpaceRcPtr cs_mod = cs->createEditableCopy();
                cs_mod->clearAliases();  // Remove any aliases to avoid collisions.
                studio_config_identities->addColorSpace(cs_mod);
            } catch (OCIO::Exception& e2) {
                // Still failed, give up
            }
        }
    }
    return studio_config_identities;
#endif
    return oiio_interop_identities;
}


std::vector<string_view>
ColorConfig::Impl::get_builtin_interop_ids() const
{
    std::vector<string_view> ids;
    if (interopconfig_) {
        for (int i = 0, e = interopconfig_->getNumColorSpaces(); i < e; ++i)
            ids.emplace_back(interopconfig_->getColorSpaceNameByIndex(i));
    }
    if (std::find(ids.begin(), ids.end(), "data") == ids.end())
        ids.emplace_back("data");
    if (std::find(ids.begin(), ids.end(), "unknown") == ids.end())
        ids.emplace_back("unknown");
    return ids;
}

string_view
ColorConfig::Impl::resolve(string_view name) const
{
    const char* namestr = c_str(name);
    auto cs             = config_->getColorSpace(namestr);
    if (cs)
        return cs->getName();

    // OCIO did not know this name as a color space, role, or alias.
    spin_rw_write_lock lock(m_mutex);

    // Check the interop identities config as well...
    auto builtin_cs = interopconfig_->getColorSpace(namestr);
    if (builtin_cs) {
        try {
            const char* equivalent_cs = OCIO::Config::IdentifyBuiltinColorSpace(
                config_, interopconfig_, builtin_cs->getName());
            if (equivalent_cs && *equivalent_cs)
                return equivalent_cs;
        } catch (OCIO::Exception& e) {
            // ignore
        }
    }

    // Maybe it's an informal alias of common names?
    if ((Strutil::iequals(name, "sRGB")
         || Strutil::iequals(name, "srgb_rec709_scene"))
        && !srgb_alias.empty())
        return srgb_alias;
    if ((Strutil::iequals(name, "lin_srgb")
         || Strutil::iequals(name, "lin_rec709")
         || Strutil::iequals(name, "lin_rec709_scene")
         || Strutil::iequals(name, "linear"))
        && lin_srgb_alias.size())
        return lin_srgb_alias;
    if ((Strutil::iequals(name, "ACEScg")
         || Strutil::iequals(name, "lin_ap1_scene"))
        && !ACEScg_alias.empty())
        return ACEScg_alias;
    if (Strutil::iequals(name, "scene_linear") && !scene_linear_alias.empty()) {
        return scene_linear_alias;
    }
    if (Strutil::iequals(name, "Rec709") && Rec709_alias.size())
        return Rec709_alias;

    return name;
}



bool
ColorConfig::equivalent(string_view color_space1,
                        string_view color_space2) const
{
    // Empty color spaces never match
    if (color_space1.empty() || color_space2.empty())
        return false;
    // Easy case: matching names are the same!
    if (Strutil::iequals(color_space1, color_space2))
        return true;

    // If "resolved" names (after converting aliases and roles to color
    // spaces) match, they are equivalent.
    color_space1 = resolve(color_space1);
    color_space2 = resolve(color_space2);
    if (color_space1.empty() || color_space2.empty())
        return false;
    if (Strutil::iequals(color_space1, color_space2))
        return true;

    // If the color spaces' flags (when masking only the bits that refer to
    // specific known color spaces) match, consider them equivalent.
    const int mask = CSInfo::is_srgb | CSInfo::is_lin_srgb | CSInfo::is_ACEScg
                     | CSInfo::is_Rec709;
    const CSInfo* csi1 = getImpl()->find(color_space1);
    const CSInfo* csi2 = getImpl()->find(color_space2);
    if (csi1 && csi2) {
        int flags1 = csi1->flags() & mask;
        int flags2 = csi2->flags() & mask;
        if ((flags1 | flags2) && csi1->flags() == csi2->flags())
            return true;
        if ((csi1->canonical.size() && csi2->canonical.size())
            && Strutil::iequals(csi1->canonical, csi2->canonical))
            return true;
    }

    return false;
}



bool
equivalent_colorspace(string_view a, string_view b)
{
    return ColorConfig::default_colorconfig().equivalent(a, b);
}



inline OCIO::BitDepth
ocio_bitdepth(TypeDesc type)
{
    if (type == TypeDesc::UINT8)
        return OCIO::BIT_DEPTH_UINT8;
    if (type == TypeDesc::UINT16)
        return OCIO::BIT_DEPTH_UINT16;
    if (type == TypeDesc::UINT32)
        return OCIO::BIT_DEPTH_UINT32;
    // N.B.: OCIOv2 also supports 10, 12, and 14 bit int, but we won't
    // ever have data in that format at this stage.
    if (type == TypeDesc::HALF)
        return OCIO::BIT_DEPTH_F16;
    if (type == TypeDesc::FLOAT)
        return OCIO::BIT_DEPTH_F32;
    return OCIO::BIT_DEPTH_UNKNOWN;
}



// Custom ColorProcessor that wraps an OpenColorIO Processor.
class ColorProcessor_OCIO final : public ColorProcessor {
public:
    ColorProcessor_OCIO(OCIO::ConstProcessorRcPtr p)
        : m_p(p)
        , m_cpuproc(p->getDefaultCPUProcessor())
    {
    }
    ~ColorProcessor_OCIO() override {}

    bool isNoOp() const override { return m_p->isNoOp(); }
    bool hasChannelCrosstalk() const override
    {
        return m_p->hasChannelCrosstalk();
    }
    void apply(float* data, int width, int height, int channels,
               stride_t chanstride, stride_t xstride,
               stride_t ystride) const override
    {
        OCIO::PackedImageDesc pid(data, width, height, channels,
                                  OCIO::BIT_DEPTH_F32,  // For now, only float
                                  chanstride, xstride, ystride);
        m_cpuproc->apply(pid);
    }

private:
    OCIO::ConstProcessorRcPtr m_p;
    OCIO::ConstCPUProcessorRcPtr m_cpuproc;
};



// ColorProcessor that implements a matrix multiply color transformation.
class ColorProcessor_Matrix final : public ColorProcessor {
public:
    ColorProcessor_Matrix(const Imath::M44f& Matrix, bool inverse)
        : ColorProcessor()
        , m_M(Matrix)
    {
        if (inverse)
            m_M = m_M.inverse();
    }
    ~ColorProcessor_Matrix() override {}

    void apply(float* data, int width, int height, int channels,
               stride_t chanstride, stride_t xstride,
               stride_t ystride) const override
    {
        using namespace simd;
        if (channels == 3 && chanstride == sizeof(float)) {
            for (int y = 0; y < height; ++y) {
                char* d = (char*)data + y * ystride;
                for (int x = 0; x < width; ++x, d += xstride) {
                    vfloat4 color;
                    color.load((float*)d, 3);
                    vfloat4 xcolor = color * m_M;
                    xcolor.store((float*)d, 3);
                }
            }
        } else if (channels >= 4 && chanstride == sizeof(float)) {
            for (int y = 0; y < height; ++y) {
                char* d = (char*)data + y * ystride;
                for (int x = 0; x < width; ++x, d += xstride) {
                    vfloat4 color;
                    color.load((float*)d);
                    vfloat4 xcolor = color * m_M;
                    xcolor.store((float*)d);
                }
            }
        } else {
            channels = std::min(channels, 4);
            for (int y = 0; y < height; ++y) {
                char* d = (char*)data + y * ystride;
                for (int x = 0; x < width; ++x, d += xstride) {
                    vfloat4 color;
                    char* dc = d;
                    for (int c = 0; c < channels; ++c, dc += chanstride)
                        color[c] = *(float*)dc;
                    vfloat4 xcolor = color * m_M;
                    for (int c = 0; c < channels; ++c, dc += chanstride)
                        *(float*)dc = xcolor[c];
                }
            }
        }
    }

private:
    simd::matrix44 m_M;
};



ColorProcessorHandle
ColorConfig::createColorProcessor(string_view inputColorSpace,
                                  string_view outputColorSpace,
                                  string_view context_key,
                                  string_view context_value) const
{
    return createColorProcessor(ustring(inputColorSpace),
                                ustring(outputColorSpace), ustring(context_key),
                                ustring(context_value));
}



ColorProcessorHandle
ColorConfig::createColorProcessor(ustring inputColorSpace,
                                  ustring outputColorSpace, ustring context_key,
                                  ustring context_value) const
{
    std::string pending_error;

    // First, look up the requested processor in the cache. If it already
    // exists, just return it.
    ColorProcCacheKey prockey(inputColorSpace, outputColorSpace, context_key,
                              context_value);
    ColorProcessorHandle handle = getImpl()->findproc(prockey);
    if (handle)
        return handle;

    // DBG("createColorProcessor {} -> {}\n", inputColorSpace,
    //                outputColorSpace);
    // Ask OCIO to make a Processor that can handle the requested
    // transformation.
    OCIO::ConstProcessorRcPtr p;
    if (getImpl()->config_ && !disable_ocio) {
        // DBG("after role substitution, {} -> {}\n", inputColorSpace,
        //                outputColorSpace);
        auto config  = getImpl()->config_;
        auto context = config->getCurrentContext();
        auto keys    = Strutil::splits(context_key, ",");
        auto values  = Strutil::splits(context_value, ",");
        if (keys.size() && values.size() && keys.size() == values.size()) {
            OCIO::ContextRcPtr ctx = context->createEditableCopy();
            for (size_t i = 0; i < keys.size(); ++i)
                ctx->setStringVar(keys[i].c_str(), values[i].c_str());
            context = ctx;
        }

        // If either the input or output color spaces are in the known
        // builtin interop identities, and not in the current config,
        // create a processor that goes through the interop config.
        auto builtin_ids = getImpl()->get_builtin_interop_ids();
        bool use_interop = false;
        bool input_matches_builtin_interop_id  = false;
        bool output_matches_builtin_interop_id = false;
        bool input_in_current_config           = true;
        bool output_in_current_config          = true;

        auto cs_in = config->getColorSpace(c_str(inputColorSpace));

        if (!cs_in) {
            input_in_current_config = false;
            if (getImpl()->interopconfig_->getColorSpace(
                    c_str(inputColorSpace))) {
                // DBG("Input color space '{}' found in interop config\n",
                //                inputColorSpace);
                input_matches_builtin_interop_id = true;
            }
        }
        auto cs_out = config->getColorSpace(c_str(outputColorSpace));
        if (!cs_out) {
            output_in_current_config = false;
            if (getImpl()->interopconfig_->getColorSpace(
                    c_str(outputColorSpace))) {
                // DBG("Output color space '{}' found in interop config\n",
                //                outputColorSpace);
                output_matches_builtin_interop_id = true;
            }
        }
        use_interop = (input_matches_builtin_interop_id
                       || output_matches_builtin_interop_id)
                      && (!input_in_current_config
                          || !output_in_current_config);

        if (use_interop) {
            auto interop_config = getImpl()->interopconfig_;
            auto src_config     = config;
            auto dst_config     = config;
            if (input_matches_builtin_interop_id)
                src_config = interop_config;
            if (output_matches_builtin_interop_id)
                dst_config = interop_config;
            try {
                p = OCIO::Config::GetProcessorFromConfigs(
                    context, src_config, c_str(inputColorSpace), context,
                    dst_config, c_str(outputColorSpace));
                getImpl()->clear_error();
            } catch (OCIO::Exception& e) {
                // Don't quit yet, remember the error and see if any of our
                // built-in knowledge of some generic spaces will save us.
                p.reset();
                pending_error = e.what();
            } catch (...) {
                p.reset();
                getImpl()->error(
                    "An unknown error occurred in OpenColorIO, getProcessor");
            }
        } else {
            inputColorSpace  = ustring(inputColorSpace);
            outputColorSpace = ustring(outputColorSpace);

            try {
                // Get the processor corresponding to this transform.
                p = getImpl()->config_->getProcessor(context,
                                                     inputColorSpace.c_str(),
                                                     outputColorSpace.c_str());
                getImpl()->clear_error();
                // DBG("Created OCIO processor '{}' -> '{}'\n",
                //                inputColorSpace, outputColorSpace);
            } catch (OCIO::Exception& e) {
                // Don't quit yet, remember the error and see if any of our
                // built-in knowledge of some generic spaces will save us.
                p.reset();
                pending_error = e.what();
                // DBG("FAILED to create OCIO processor '{}' -> '{}'\n",
                //                inputColorSpace, outputColorSpace);
            } catch (...) {
                p.reset();
                getImpl()->error(
                    "An unknown error occurred in OpenColorIO, getProcessor");
            }
        }
        if (p && !p->isNoOp()) {
            // If we got a valid processor that does something useful,
            // return it now. If it boils down to a no-op, give a second
            // chance below to recognize it as a special case.
            handle = ColorProcessorHandle(new ColorProcessor_OCIO(p));
            // DBG("OCIO processor '{}' -> '{}' is NOT NoOp, handle = {}\n",
            //                inputColorSpace, outputColorSpace, (bool)handle);
        }
    }

    if (!handle && p) {
        // If we found a processor from OCIO, even if it was a NoOp, and we
        // still don't have a better idea, return it.
        handle = ColorProcessorHandle(new ColorProcessor_OCIO(p));
    }

    if (pending_error.size())
        getImpl()->error("{}", pending_error);

    return getImpl()->addproc(prockey, handle);
}



ColorProcessorHandle
ColorConfig::createLookTransform(string_view looks, string_view inputColorSpace,
                                 string_view outputColorSpace, bool inverse,
                                 string_view context_key,
                                 string_view context_value) const
{
    return createLookTransform(ustring(looks), ustring(inputColorSpace),
                               ustring(outputColorSpace), inverse,
                               ustring(context_key), ustring(context_value));
}



ColorProcessorHandle
ColorConfig::createLookTransform(ustring looks, ustring inputColorSpace,
                                 ustring outputColorSpace, bool inverse,
                                 ustring context_key,
                                 ustring context_value) const
{
    // First, look up the requested processor in the cache. If it already
    // exists, just return it.
    ColorProcCacheKey prockey(inputColorSpace, outputColorSpace, context_key,
                              context_value, looks, ustring() /*display*/,
                              ustring() /*view*/, ustring() /*file*/,
                              ustring() /*namedtransform*/, inverse);
    ColorProcessorHandle handle = getImpl()->findproc(prockey);
    if (handle)
        return handle;

    // Ask OCIO to make a Processor that can handle the requested
    // transformation.

    // TODO: Handle the case where either inputColorSpace or outputColorSpace
    // is a builtin interop identity, similar to createColorProcessor.
    if (getImpl()->config_ && !disable_ocio) {
        OCIO::ConstConfigRcPtr config      = getImpl()->config_;
        OCIO::LookTransformRcPtr transform = OCIO::LookTransform::Create();
        transform->setLooks(looks.c_str());
        OCIO::TransformDirection dir;
        if (inverse) {
            // The TRANSFORM_DIR_INVERSE applies an inverse for the
            // end-to-end transform, which would otherwise do dst->inv
            // look -> src.  This is an unintuitive result for the
            // artist (who would expect in, out to remain unchanged), so
            // we account for that here by flipping src/dst
            transform->setSrc(c_str(resolve(outputColorSpace)));
            transform->setDst(c_str(resolve(inputColorSpace)));
            dir = OCIO::TRANSFORM_DIR_INVERSE;
        } else {  // forward
            transform->setSrc(c_str(resolve(inputColorSpace)));
            transform->setDst(c_str(resolve(outputColorSpace)));
            dir = OCIO::TRANSFORM_DIR_FORWARD;
        }
        auto context = config->getCurrentContext();
        auto keys    = Strutil::splits(context_key, ",");
        auto values  = Strutil::splits(context_value, ",");
        if (keys.size() && values.size() && keys.size() == values.size()) {
            OCIO::ContextRcPtr ctx = context->createEditableCopy();
            for (size_t i = 0; i < keys.size(); ++i)
                ctx->setStringVar(keys[i].c_str(), values[i].c_str());
            context = ctx;
        }

        OCIO::ConstProcessorRcPtr p;
        try {
            // Get the processor corresponding to this transform.
            p = getImpl()->config_->getProcessor(context, transform, dir);
            getImpl()->clear_error();
            handle = ColorProcessorHandle(new ColorProcessor_OCIO(p));
        } catch (OCIO::Exception& e) {
            getImpl()->error(e.what());
        } catch (...) {
            getImpl()->error(
                "An unknown error occurred in OpenColorIO, getProcessor");
        }
    }

    return getImpl()->addproc(prockey, handle);
}



ColorProcessorHandle
ColorConfig::createDisplayTransform(string_view display, string_view view,
                                    string_view inputColorSpace,
                                    string_view looks, bool inverse,
                                    string_view context_key,
                                    string_view context_value) const
{
    return createDisplayTransform(ustring(display), ustring(view),
                                  ustring(inputColorSpace), ustring(looks),
                                  inverse, ustring(context_key),
                                  ustring(context_value));
}



ColorProcessorHandle
ColorConfig::createDisplayTransform(ustring display, ustring view,
                                    ustring inputColorSpace, ustring looks,
                                    bool inverse, ustring context_key,
                                    ustring context_value) const
{
    if (display.empty() || display == "default")
        display = getDefaultDisplayName();
    if (view.empty() || view == "default")
        view = getDefaultViewName(display, inputColorSpace);
    // First, look up the requested processor in the cache. If it already
    // exists, just return it.
    ColorProcCacheKey prockey(inputColorSpace, ustring() /*outputColorSpace*/,
                              context_key, context_value, looks, display, view,
                              ustring() /*file*/, ustring() /*namedtransform*/,
                              inverse);
    ColorProcessorHandle handle = getImpl()->findproc(prockey);
    if (handle)
        return handle;

    // Ask OCIO to make a Processor that can handle the requested
    // transformation.

    OCIO::ConstConfigRcPtr config = getImpl()->config_;
    auto transform                = OCIO::DisplayViewTransform::Create();
    auto legacy_viewing_pipeline  = OCIO::LegacyViewingPipeline::Create();
    OCIO::TransformDirection dir  = inverse ? OCIO::TRANSFORM_DIR_INVERSE
                                            : OCIO::TRANSFORM_DIR_FORWARD;
    string_view original_input_cs = c_str(inputColorSpace);


    auto cs_in = config->getColorSpace(c_str(inputColorSpace));
    if (!cs_in) {
        if (getImpl()->interopconfig_->getColorSpace(c_str(inputColorSpace))) {
            inputColorSpace = ustring(OCIO::ROLE_SCENE_LINEAR);
        }
    }

    transform->setSrc(inputColorSpace.c_str());
    transform->setDisplay(display.c_str());
    transform->setView(view.c_str());
    transform->setDirection(dir);
    legacy_viewing_pipeline->setDisplayViewTransform(transform);
    if (looks.size()) {
        legacy_viewing_pipeline->setLooksOverride(looks.c_str());
        legacy_viewing_pipeline->setLooksOverrideEnabled(true);
    }
    auto context = config->getCurrentContext();
    auto keys    = Strutil::splits(context_key, ",");
    auto values  = Strutil::splits(context_value, ",");
    if (keys.size() && values.size() && keys.size() == values.size()) {
        OCIO::ContextRcPtr ctx = context->createEditableCopy();
        for (size_t i = 0; i < keys.size(); ++i)
            ctx->setStringVar(keys[i].c_str(), values[i].c_str());
        context = ctx;
    }

    OCIO::ConstProcessorRcPtr p;
    try {
        // Get the processor corresponding to this transform.
        p = legacy_viewing_pipeline->getProcessor(config, context);
        getImpl()->clear_error();
        // If the original input color space doesn't match InputColorSpace,
        // we need to prepend a conversion to InputColorSpace.
        if (!Strutil::iequals(original_input_cs, inputColorSpace)) {
            auto p_xform      = p->createGroupTransform();
            auto pretransform = OCIO::Config::GetProcessorFromConfigs(
                                    context, getImpl()->interopconfig_,
                                    c_str(original_input_cs), context, config,
                                    c_str(inputColorSpace))
                                    ->createGroupTransform();

            if (inverse) {
                pretransform->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
                p_xform->appendTransform(pretransform);
            } else {
                p_xform->prependTransform(pretransform);
            }
            p = config->getProcessor(p_xform);
        }
        handle = ColorProcessorHandle(new ColorProcessor_OCIO(p));
    } catch (OCIO::Exception& e) {
        getImpl()->error(e.what());
    } catch (...) {
        getImpl()->error(
            "An unknown error occurred in OpenColorIO, getProcessor");
    }
    return getImpl()->addproc(prockey, handle);
}



ColorProcessorHandle
ColorConfig::createFileTransform(string_view name, bool inverse) const
{
    return createFileTransform(ustring(name), inverse);
}



ColorProcessorHandle
ColorConfig::createFileTransform(ustring name, bool inverse) const
{
    // First, look up the requested processor in the cache. If it already
    // exists, just return it.
    ColorProcCacheKey prockey(ustring() /*inputColorSpace*/,
                              ustring() /*outputColorSpace*/,
                              ustring() /*context_key*/,
                              ustring() /*context_value*/, ustring() /*looks*/,
                              ustring() /*display*/, ustring() /*view*/,
                              ustring() /*file*/, name, inverse);
    ColorProcessorHandle handle = getImpl()->findproc(prockey);
    if (handle)
        return handle;

    // Ask OCIO to make a Processor that can handle the requested
    // transformation.
    OCIO::ConstConfigRcPtr config = getImpl()->config_;
    // If no config was found, config_ will be null. But that shouldn't
    // stop us for a filetransform, which doesn't need color spaces anyway.
    // Just use the default current config, it'll be freed when we exit.
    if (!config)
        config = ocio_current_config;
    if (config) {
        OCIO::FileTransformRcPtr transform = OCIO::FileTransform::Create();
        transform->setSrc(name.c_str());
        transform->setInterpolation(OCIO::INTERP_BEST);
        OCIO::TransformDirection dir    = inverse ? OCIO::TRANSFORM_DIR_INVERSE
                                                  : OCIO::TRANSFORM_DIR_FORWARD;
        OCIO::ConstContextRcPtr context = config->getCurrentContext();
        OCIO::ConstProcessorRcPtr p;
        try {
            // Get the processor corresponding to this transform.
            p = config->getProcessor(context, transform, dir);
            getImpl()->clear_error();
            handle = ColorProcessorHandle(new ColorProcessor_OCIO(p));
        } catch (OCIO::Exception& e) {
            getImpl()->error(e.what());
        } catch (...) {
            getImpl()->error(
                "An unknown error occurred in OpenColorIO, getProcessor");
        }
    }

    return getImpl()->addproc(prockey, handle);
}



ColorProcessorHandle
ColorConfig::createNamedTransform(string_view name, bool inverse,
                                  string_view context_key,
                                  string_view context_value) const
{
    return createNamedTransform(ustring(name), inverse, ustring(context_key),
                                ustring(context_value));
}



ColorProcessorHandle
ColorConfig::createNamedTransform(ustring name, bool inverse,
                                  ustring context_key,
                                  ustring context_value) const
{
    // First, look up the requested processor in the cache. If it already
    // exists, just return it.
    ColorProcCacheKey prockey(ustring() /*inputColorSpace*/,
                              ustring() /*outputColorSpace*/, context_key,
                              context_value, ustring() /*looks*/,
                              ustring() /*display*/, ustring() /*view*/,
                              ustring() /*file*/, name, inverse);
    ColorProcessorHandle handle = getImpl()->findproc(prockey);
    if (handle)
        return handle;

    // Ask OCIO to make a Processor that can handle the requested
    // transformation.
    if (getImpl()->config_ && !disable_ocio) {
        OCIO::ConstConfigRcPtr config = getImpl()->config_;
        auto transform                = config->getNamedTransform(name.c_str());
        OCIO::TransformDirection dir  = inverse ? OCIO::TRANSFORM_DIR_INVERSE
                                                : OCIO::TRANSFORM_DIR_FORWARD;
        auto context                  = config->getCurrentContext();
        auto keys                     = Strutil::splits(context_key, ",");
        auto values                   = Strutil::splits(context_value, ",");
        if (keys.size() && values.size() && keys.size() == values.size()) {
            OCIO::ContextRcPtr ctx = context->createEditableCopy();
            for (size_t i = 0; i < keys.size(); ++i)
                ctx->setStringVar(keys[i].c_str(), values[i].c_str());
            context = ctx;
        }

        OCIO::ConstProcessorRcPtr p;
        try {
            // Get the processor corresponding to this transform.
            p = config->getProcessor(context, transform, dir);
            getImpl()->clear_error();
            handle = ColorProcessorHandle(new ColorProcessor_OCIO(p));
        } catch (OCIO::Exception& e) {
            getImpl()->error(e.what());
        } catch (...) {
            getImpl()->error(
                "An unknown error occurred in OpenColorIO, getProcessor");
        }
    }

    return getImpl()->addproc(prockey, handle);
}



ColorProcessorHandle
ColorConfig::createMatrixTransform(M44fParam M, bool inverse) const
{
    return ColorProcessorHandle(
        new ColorProcessor_Matrix(*(const Imath::M44f*)M.data(), inverse));
}



string_view
ColorConfig::getColorSpaceFromFilepath(string_view str) const
{
    if (getImpl() && getImpl()->config_) {
        std::string s(str);
        string_view r = getImpl()->config_->getColorSpaceFromFilepath(
            s.c_str());
        return r;
    }
    // Fall back on parseColorSpaceFromString
    return parseColorSpaceFromString(str);
}

string_view
ColorConfig::getColorSpaceFromFilepath(string_view str, string_view default_cs,
                                       bool cs_name_match) const
{
    if (getImpl() && getImpl()->config_) {
        std::string s(str);
        string_view r = getImpl()->config_->getColorSpaceFromFilepath(
            s.c_str());
        if (!getImpl()->config_->filepathOnlyMatchesDefaultRule(s.c_str()))
            return r;
    }
    if (cs_name_match) {
        string_view parsed = parseColorSpaceFromString(str);
        if (parsed.size())
            return parsed;
    }
    return default_cs;
}

bool
ColorConfig::filepathOnlyMatchesDefaultRule(string_view str) const
{
    return getImpl()->config_->filepathOnlyMatchesDefaultRule(c_str(str));
}

string_view
ColorConfig::parseColorSpaceFromString(string_view str) const
{
    // Reproduce the logic in OCIO v1 parseColorSpaceFromString

    if (str.empty())
        return "";

    // Get the colorspace names, sorted shortest-to-longest
    auto names = getColorSpaceNames();
    std::sort(names.begin(), names.end(),
              [](const std::string& a, const std::string& b) {
                  return a.length() < b.length();
              });

    // See if it matches a LUT name.
    // This is the position of the RIGHT end of the colorspace substring,
    // not the left
    size_t rightMostColorPos = std::string::npos;
    std::string rightMostColorspace;

    // Find the right-most occurrence within the string for each colorspace.
    for (auto&& csname : names) {
        // find right-most extension matched in filename
        size_t pos = Strutil::irfind(str, csname);
        if (pos == std::string::npos)
            continue;

        // If we have found a match, move the pointer over to the right end
        // of the substring.  This will allow us to find the longest name
        // that matches the rightmost colorspace
        pos += csname.size();

        if (rightMostColorPos == std::string::npos
            || pos >= rightMostColorPos) {
            rightMostColorPos   = pos;
            rightMostColorspace = csname;
        }
    }
    return string_view(ustring(rightMostColorspace));
}


//////////////////////////////////////////////////////////////////////////
//
// Color Interop ID

namespace {
enum class CICPPrimaries : int {
    Rec709      = 1,
    Unspecified = 2,
    Rec2020     = 9,
    XYZD65      = 10,
    P3D65       = 12,
};

enum class CICPTransfer : int {
    BT709       = 1,
    Unspecified = 2,
    Gamma22     = 4,
    Linear      = 8,
    sRGB        = 13,
    PQ          = 16,
    Gamma26     = 17,
    HLG         = 18,
};

enum class CICPMatrix : int {
    RGB         = 0,
    BT709       = 1,
    Unspecified = 2,
    Rec2020_NCL = 9,
    Rec2020_CL  = 10,
};

enum class CICPRange : int {
    Narrow = 0,
    Full   = 1,
};

struct ColorInteropID {
    constexpr ColorInteropID(const char* interop_id)
        : interop_id(interop_id)
        , cicp({ 0, 0, 0, 0 })
        , has_cicp(false)
    {
    }

    constexpr ColorInteropID(const char* interop_id, CICPPrimaries primaries,
                             CICPTransfer transfer, CICPMatrix matrix)
        : interop_id(interop_id)
        , cicp({ int(primaries), int(transfer), int(matrix),
                 int(CICPRange::Full) })
        , has_cicp(true)
    {
    }

    const char* interop_id;
    std::array<int, 4> cicp;
    bool has_cicp;
};

// Mapping between color interop ID and CICP, based on Color Interop Forum
// recommendations.
constexpr ColorInteropID color_interop_ids[] = {
    // Display referred interop IDs first so they are the default in automatic
    // conversion from CICP to interop ID.
    { "srgb_rec709_display", CICPPrimaries::Rec709, CICPTransfer::sRGB,
      CICPMatrix::BT709 },
    { "g24_rec709_display", CICPPrimaries::Rec709, CICPTransfer::BT709,
      CICPMatrix::BT709 },
    { "srgb_p3d65_display", CICPPrimaries::P3D65, CICPTransfer::sRGB,
      CICPMatrix::BT709 },
    { "srgbe_p3d65_display", CICPPrimaries::P3D65, CICPTransfer::sRGB,
      CICPMatrix::BT709 },
    { "pq_p3d65_display", CICPPrimaries::P3D65, CICPTransfer::PQ,
      CICPMatrix::Rec2020_NCL },
    { "pq_rec2020_display", CICPPrimaries::Rec2020, CICPTransfer::PQ,
      CICPMatrix::Rec2020_NCL },
    { "hlg_rec2020_display", CICPPrimaries::Rec2020, CICPTransfer::HLG,
      CICPMatrix::Rec2020_NCL },
    { "g22_rec709_display", CICPPrimaries::Rec709, CICPTransfer::Gamma22,
      CICPMatrix::BT709 },
    // No CICP code for Adobe RGB primaries.
    { "g22_adobergb_display" },
    { "g26_p3d65_display", CICPPrimaries::P3D65, CICPTransfer::Gamma26,
      CICPMatrix::BT709 },
    { "g26_xyzd65_display", CICPPrimaries::XYZD65, CICPTransfer::Gamma26,
      CICPMatrix::Unspecified },
    { "pq_xyzd65_display", CICPPrimaries::XYZD65, CICPTransfer::PQ,
      CICPMatrix::Unspecified },

    // Some scene referred interop IDs can be represented by CICP
    { "lin_ap1_scene" },
    { "lin_ap0_scene" },
    { "lin_rec709_scene", CICPPrimaries::Rec709, CICPTransfer::Linear,
      CICPMatrix::BT709 },
    { "lin_p3d65_scene", CICPPrimaries::P3D65, CICPTransfer::Linear,
      CICPMatrix::BT709 },
    { "lin_rec2020_scene", CICPPrimaries::Rec2020, CICPTransfer::Linear,
      CICPMatrix::Rec2020_CL },
    { "lin_adobergb_scene" },
    { "lin_ciexyzd65_scene", CICPPrimaries::XYZD65, CICPTransfer::Linear,
      CICPMatrix::Unspecified },
    { "srgb_rec709_scene", CICPPrimaries::Rec709, CICPTransfer::sRGB,
      CICPMatrix::BT709 },
    { "g22_rec709_scene", CICPPrimaries::Rec709, CICPTransfer::Gamma22,
      CICPMatrix::BT709 },
    { "g18_rec709_scene" },
    { "srgb_ap1_scene" },
    { "g22_ap1_scene" },
    { "srgb_p3d65_scene", CICPPrimaries::P3D65, CICPTransfer::sRGB,
      CICPMatrix::BT709 },
    { "g22_adobergb_scene" },

    // Other standard CIF interop IDs
    { "data" },
    { "unknown", CICPPrimaries::Unspecified, CICPTransfer::Unspecified,
      CICPMatrix::Unspecified },
};
}  // namespace

string_view
ColorConfig::get_color_interop_id(string_view colorspace, bool strict) const
{
    if (colorspace.empty())
        return colorspace;
    string_view interop_id;
    auto config        = getImpl()->config_;
    auto cs            = config->getColorSpace(c_str(colorspace));
    auto interopconfig = getImpl()->interopconfig_;
    if (!cs) {
        // Does 'colorspace' value match the name of a built-in interop ID?
        auto interop_cs = interopconfig->getColorSpace(c_str(colorspace));
        return (interop_cs) ? interop_cs->getName() : "";
    }
    if (cs->isData())
        return "data";
#if OCIO_VERSION_HEX >= MAKE_OCIO_VERSION_HEX(2, 5, 0)
    interop_id = cs->getInteropID();
    if (interop_id)
        return interop_id;
#endif
    for (const ColorInteropID& interop : color_interop_ids) {
        if (equivalent(colorspace, interop.interop_id)) {
            return interop.interop_id;
        }
    }


    // In strict mode, only return interop ID if explicitly defined
    if (strict || interop_id.size())
        return interop_id;

    // Check to see if this colorspace's name or any of its aliases
    // match a known interop ID.
    auto interop_cs = interopconfig->getColorSpace(cs->getName());
    if (interop_cs)
        return interop_cs->getName();
    for (int i = 0; i < cs->getNumAliases(); ++i) {
        string_view alias = cs->getAlias(i);
        interop_cs        = interopconfig->getColorSpace(c_str(alias));
        if (interop_cs)
            return interop_cs->getName();
    }

    auto interop_ids = getImpl()->get_builtin_interop_ids();
    // Finally, see if we can match the cs definition to
    // a known equivalent interop ID definition
    for (auto&& this_id : interop_ids) {
        if (equivalent(cs->getName(), this_id)) {
            return this_id;
        }
    }
    return "";
}

string_view
ColorConfig::get_color_interop_id(const int cicp[4]) const
{
    for (const ColorInteropID& interop : color_interop_ids) {
        if (interop.has_cicp && interop.cicp[0] == cicp[0]
            && interop.cicp[1] == cicp[1]) {
            return interop.interop_id;
        }
    }
    return "";
}

cspan<int>
ColorConfig::get_cicp(string_view colorspace) const
{
    string_view interop_id = get_color_interop_id(colorspace);
    if (!interop_id.empty()) {
        for (const ColorInteropID& interop : color_interop_ids) {
            if (interop.has_cicp && interop_id == interop.interop_id) {
                return interop.cicp;
            }
        }
    }
    return cspan<int>();
}


//////////////////////////////////////////////////////////////////////////
//
// Image Processing Implementations


bool
ImageBufAlgo::colorconvert(ImageBuf& dst, const ImageBuf& src, string_view from,
                           string_view to, bool unpremult,
                           string_view context_key, string_view context_value,
                           const ColorConfig* colorconfig, ROI roi,
                           int nthreads)
{
    OIIO::pvt::LoggedTimer logtime("IBA::colorconvert");
    if (from.empty() || from == "current") {
        from = src.spec().get_string_attribute("oiio:Colorspace",
                                               "scene_linear");
    }
    if (from.empty() || to.empty()) {
        dst.errorfmt("Unknown color space name");
        return false;
    }
    ColorProcessorHandle processor;
    {
        if (!colorconfig)
            colorconfig = &ColorConfig::default_colorconfig();
        processor
            = colorconfig->createColorProcessor(colorconfig->resolve(from),
                                                colorconfig->resolve(to),
                                                context_key, context_value);
        if (!processor) {
            if (colorconfig->has_error())
                dst.errorfmt("{}", colorconfig->geterror());
            else
                dst.errorfmt(
                    "Could not construct the color transform {} -> {} (unknown error)",
                    from, to);
            return false;
        }
    }

    logtime.stop(-1);  // transition to other colorconvert
    bool ok = colorconvert(dst, src, processor.get(), unpremult, roi, nthreads);
    if (ok) {
        // DBG("done, setting output colorspace to {}\n", to);
        dst.specmod().set_colorspace(to);
    }
    return ok;
}



ImageBuf
ImageBufAlgo::colorconvert(const ImageBuf& src, string_view from,
                           string_view to, bool unpremult,
                           string_view context_key, string_view context_value,
                           const ColorConfig* colorconfig, ROI roi,
                           int nthreads)
{
    ImageBuf result;
    bool ok = colorconvert(result, src, from, to, unpremult, context_key,
                           context_value, colorconfig, roi, nthreads);
    if (!ok && !result.has_error())
        result.errorfmt("ImageBufAlgo::colorconvert() error");
    return result;
}



bool
ImageBufAlgo::colormatrixtransform(ImageBuf& dst, const ImageBuf& src,
                                   M44fParam M, bool unpremult, ROI roi,
                                   int nthreads)
{
    OIIO::pvt::LoggedTimer logtime("IBA::colormatrixtransform");
    ColorProcessorHandle processor
        = ColorConfig::default_colorconfig().createMatrixTransform(M);
    logtime.stop();  // transition to other colorconvert
    bool ok = colorconvert(dst, src, processor.get(), unpremult, roi, nthreads);
    return ok;
}



ImageBuf
ImageBufAlgo::colormatrixtransform(const ImageBuf& src, M44fParam M,
                                   bool unpremult, ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = colormatrixtransform(result, src, M, unpremult, roi, nthreads);
    if (!ok && !result.has_error())
        result.errorfmt("ImageBufAlgo::colormatrixtransform() error");
    return result;
}



template<class Rtype, class Atype>
static bool
colorconvert_impl(ImageBuf& R, const ImageBuf& A,
                  const ColorProcessor* processor, bool unpremult, ROI roi,
                  int nthreads)
{
    using namespace ImageBufAlgo;
    using namespace simd;
    // Only process up to, and including, the first 4 channels.  This
    // does let us process images with fewer than 4 channels, which is
    // the intent.
    int channelsToCopy = std::min(4, roi.nchannels());
    if (channelsToCopy < 4)
        unpremult = false;
    // clang-format off
    parallel_image(
        roi, paropt(nthreads),
        [&, unpremult, channelsToCopy, processor](ROI roi) {
            int width = roi.width();
            // Temporary space to hold one RGBA scanline
            vfloat4* scanline;
            OIIO_ALLOCATE_STACK_OR_HEAP(scanline, vfloat4, width);
            float* alpha;
            OIIO_ALLOCATE_STACK_OR_HEAP(alpha, float, width);
            const float fltmin = std::numeric_limits<float>::min();
            ImageBuf::ConstIterator<Atype> a(A, roi);
            ImageBuf::Iterator<Rtype> r(R, roi);
            for (int k = roi.zbegin; k < roi.zend; ++k) {
                for (int j = roi.ybegin; j < roi.yend; ++j) {
                    // Load the scanline
                    a.rerange(roi.xbegin, roi.xend, j, j + 1, k, k + 1);
                    for (int i = 0; !a.done(); ++a, ++i) {
                        vfloat4 v(0.0f);
                        for (int c = 0; c < channelsToCopy; ++c)
                            v[c] = a[c];
                        if (channelsToCopy == 1)
                            v[2] = v[1] = v[0];
                        scanline[i] = v;
                    }

                    // Optionally unpremult. Be careful of alpha==0 pixels,
                    // preserve their color rather than div-by-zero.
                    if (unpremult) {
                        for (int i = 0; i < width; ++i) {
                            float a  = extract<3>(scanline[i]);
                            alpha[i] = a;
                            a        = a >= fltmin ? a : 1.0f;
                            scanline[i] /= vfloat4(a,a,a,1.0f);
                        }
                    }

                    // Apply the color transformation in place
                    processor->apply((float*)&scanline[0], width, 1, 4,
                                     sizeof(float), 4 * sizeof(float),
                                     width * 4 * sizeof(float));

                    // Optionally re-premult. Be careful of alpha==0 pixels,
                    // preserve their value rather than crushing to black.
                    if (unpremult) {
                        for (int i = 0; i < width; ++i) {
                            float a  = alpha[i];
                            a        = a >= fltmin ? a : 1.0f;
                            scanline[i] *= vfloat4(a,a,a,1.0f);
                        }
                    }

                    // Store the scanline
                    float* dstPtr = (float*)&scanline[0];
                    r.rerange(roi.xbegin, roi.xend, j, j + 1, k, k + 1);
                    for (; !r.done(); ++r, dstPtr += 4)
                        for (int c = 0; c < channelsToCopy; ++c)
                            r[c] = dstPtr[c];
                    if (channelsToCopy < roi.chend && (&R != &A)) {
                        // If there are "leftover" channels, just copy them
                        // unaltered from the source.
                        a.rerange(roi.xbegin, roi.xend, j, j + 1, k, k + 1);
                        r.rerange(roi.xbegin, roi.xend, j, j + 1, k, k + 1);
                        for (; !r.done(); ++r, ++a)
                            for (int c = channelsToCopy; c < roi.chend; ++c)
                                r[c] = 0.5 + 10 * a[c];
                    }
                }
            }
        });
    // clang-format on
    return true;
}



// Specialized version where both buffers are in memory (not cache based),
// float data, and we are dealing with 4 channels.
static bool
colorconvert_impl_float_rgba(ImageBuf& R, const ImageBuf& A,
                             const ColorProcessor* processor, bool unpremult,
                             ROI roi, int nthreads)
{
    using namespace ImageBufAlgo;
    using namespace simd;
    OIIO_ASSERT(R.localpixels() && A.localpixels()
                && R.spec().format == TypeFloat && A.spec().format == TypeFloat
                && R.nchannels() == 4 && A.nchannels() == 4);
    parallel_image(roi, paropt(nthreads), [&](ROI roi) {
        int width = roi.width();
        // Temporary space to hold one RGBA scanline
        vfloat4* scanline;
        OIIO_ALLOCATE_STACK_OR_HEAP(scanline, vfloat4, width);
        float* alpha;
        OIIO_ALLOCATE_STACK_OR_HEAP(alpha, float, width);
        const float fltmin = std::numeric_limits<float>::min();
        for (int k = roi.zbegin; k < roi.zend; ++k) {
            for (int j = roi.ybegin; j < roi.yend; ++j) {
                // Load the scanline
                memcpy((void*)scanline, A.pixeladdr(roi.xbegin, j, k),
                       width * 4 * sizeof(float));
                // Optionally unpremult
                if (unpremult) {
                    for (int i = 0; i < width; ++i) {
                        vfloat4 p(scanline[i]);
                        float a  = extract<3>(p);
                        alpha[i] = a;
                        a        = a >= fltmin ? a : 1.0f;
                        if (a == 1.0f)
                            scanline[i] = p;
                        else
                            scanline[i] = p / vfloat4(a, a, a, 1.0f);
                    }
                }

                // Apply the color transformation in place
                processor->apply((float*)&scanline[0], width, 1, 4,
                                 sizeof(float), 4 * sizeof(float),
                                 width * 4 * sizeof(float));

                // Optionally premult
                if (unpremult) {
                    for (int i = 0; i < width; ++i) {
                        vfloat4 p(scanline[i]);
                        float a = alpha[i];
                        a       = a >= fltmin ? a : 1.0f;
                        p *= vfloat4(a, a, a, 1.0f);
                        scanline[i] = p;
                    }
                }
                memcpy(R.pixeladdr(roi.xbegin, j, k), scanline,
                       width * 4 * sizeof(float));  //NOSONAR
            }
        }
    });
    return true;
}



bool
ImageBufAlgo::colorconvert(ImageBuf& dst, const ImageBuf& src,
                           const ColorProcessor* processor, bool unpremult,
                           ROI roi, int nthreads)
{
    OIIO::pvt::LoggedTimer logtime("IBA::colorconvert");
    // If the processor is NULL, return false (error)
    if (!processor) {
        dst.errorfmt(
            "Passed NULL ColorProcessor to colorconvert() [probable application bug]");
        return false;
    }

    // If the processor is a no-op and the conversion is being done
    // in place, no work needs to be done. Early exit.
    if (processor->isNoOp() && (&dst == &src))
        return true;

    if (!IBAprep(roi, &dst, &src))
        return false;

    // If the processor is a no-op (and it's not an in-place conversion),
    // use copy() to simplify the operation.
    if (processor->isNoOp()) {
        logtime.stop();  // transition to copy
        return ImageBufAlgo::copy(dst, src, TypeUnknown, roi, nthreads);
    }

    if (unpremult && src.spec().alpha_channel >= 0
        && src.spec().get_int_attribute("oiio:UnassociatedAlpha") != 0) {
        // If we appear to be operating on an image that already has
        // unassociated alpha, don't do a redundant unpremult step.
        unpremult = false;
    }

    if (dst.localpixels() && src.localpixels() && dst.spec().format == TypeFloat
        && src.spec().format == TypeFloat && dst.nchannels() == 4
        && src.nchannels() == 4) {
        return colorconvert_impl_float_rgba(dst, src, processor, unpremult, roi,
                                            nthreads);
    }

    bool ok = true;
    OIIO_DISPATCH_COMMON_TYPES2(ok, "colorconvert", colorconvert_impl,
                                dst.spec().format, src.spec().format, dst, src,
                                processor, unpremult, roi, nthreads);
    return ok;
}



ImageBuf
ImageBufAlgo::colorconvert(const ImageBuf& src, const ColorProcessor* processor,
                           bool unpremult, ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = colorconvert(result, src, processor, unpremult, roi, nthreads);
    if (!ok && !result.has_error())
        result.errorfmt("ImageBufAlgo::colorconvert() error");
    return result;
}



bool
ImageBufAlgo::ociolook(ImageBuf& dst, const ImageBuf& src, string_view looks,
                       string_view from, string_view to, bool unpremult,
                       bool inverse, string_view key, string_view value,
                       const ColorConfig* colorconfig, ROI roi, int nthreads)
{
    OIIO::pvt::LoggedTimer logtime("IBA::ociolook");
    if (from.empty() || from == "current") {
        auto linearspace = colorconfig->resolve("scene_linear");
        from = src.spec().get_string_attribute("oiio:Colorspace", linearspace);
    }
    if (to.empty() || to == "current") {
        auto linearspace = colorconfig->resolve("scene_linear");
        to = src.spec().get_string_attribute("oiio:Colorspace", linearspace);
    }
    if (from.empty() || to.empty()) {
        dst.errorfmt("Unknown color space name");
        return false;
    }
    ColorProcessorHandle processor;
    {
        if (!colorconfig)
            colorconfig = &ColorConfig::default_colorconfig();
        processor = colorconfig->createLookTransform(looks,
                                                     colorconfig->resolve(from),
                                                     colorconfig->resolve(to),
                                                     inverse, key, value);
        if (!processor) {
            if (colorconfig->has_error())
                dst.errorfmt("{}", colorconfig->geterror());
            else
                dst.errorfmt(
                    "Could not construct the color transform (unknown error)");
            return false;
        }
    }

    logtime.stop();  // transition to colorconvert
    bool ok = colorconvert(dst, src, processor.get(), unpremult, roi, nthreads);
    if (ok)
        dst.specmod().set_colorspace(to);
    return ok;
}



ImageBuf
ImageBufAlgo::ociolook(const ImageBuf& src, string_view looks, string_view from,
                       string_view to, bool unpremult, bool inverse,
                       string_view key, string_view value,
                       const ColorConfig* colorconfig, ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = ociolook(result, src, looks, from, to, unpremult, inverse, key,
                       value, colorconfig, roi, nthreads);
    if (!ok && !result.has_error())
        result.errorfmt("ImageBufAlgo::ociolook() error");
    return result;
}



bool
ImageBufAlgo::ociodisplay(ImageBuf& dst, const ImageBuf& src,
                          string_view display, string_view view,
                          string_view from, string_view looks, bool unpremult,
                          bool inverse, string_view key, string_view value,
                          const ColorConfig* colorconfig, ROI roi, int nthreads)
{
    OIIO::pvt::LoggedTimer logtime("IBA::ociodisplay");
    ColorProcessorHandle processor;
    {
        if (!colorconfig)
            colorconfig = &ColorConfig::default_colorconfig();
        if (from.empty() || from == "current") {
            auto linearspace = colorconfig->resolve("scene_linear");
            from = src.spec().get_string_attribute("oiio:ColorSpace",
                                                   linearspace);
        }
        if (from.empty()) {
            dst.errorfmt("Unknown color space name");
            return false;
        }
        processor
            = colorconfig->createDisplayTransform(display, view,
                                                  colorconfig->resolve(from),
                                                  looks, inverse, key, value);
        if (!processor) {
            if (colorconfig->has_error())
                dst.errorfmt("{}", colorconfig->geterror());
            else
                dst.errorfmt(
                    "Could not construct the color transform (unknown error)");
            return false;
        }
    }

    logtime.stop();  // transition to colorconvert
    bool ok = colorconvert(dst, src, processor.get(), unpremult, roi, nthreads);
    if (ok) {
        if (inverse)
            dst.specmod().set_colorspace(colorconfig->resolve(from));
        else {
            if (display.empty() || display == "default")
                display = colorconfig->getDefaultDisplayName();
            if (view.empty() || view == "default")
                view = colorconfig->getDefaultViewName(display,
                                                       colorconfig->resolve(
                                                           from));
            dst.specmod().set_colorspace(
                colorconfig->getDisplayViewColorSpaceName(display, view));
        }
    }
    return ok;
}



ImageBuf
ImageBufAlgo::ociodisplay(const ImageBuf& src, string_view display,
                          string_view view, string_view from, string_view looks,
                          bool unpremult, bool inverse, string_view key,
                          string_view value, const ColorConfig* colorconfig,
                          ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = ociodisplay(result, src, display, view, from, looks, unpremult,
                          inverse, key, value, colorconfig, roi, nthreads);
    if (!ok && !result.has_error())
        result.errorfmt("ImageBufAlgo::ociodisplay() error");
    return result;
}



bool
ImageBufAlgo::ociofiletransform(ImageBuf& dst, const ImageBuf& src,
                                string_view name, bool unpremult, bool inverse,
                                const ColorConfig* colorconfig, ROI roi,
                                int nthreads)
{
    OIIO::pvt::LoggedTimer logtime("IBA::ociofiletransform");
    if (name.empty()) {
        dst.errorfmt("Unknown filetransform name");
        return false;
    }
    ColorProcessorHandle processor;
    {
        if (!colorconfig)
            colorconfig = &ColorConfig::default_colorconfig();
        processor = colorconfig->createFileTransform(name, inverse);
        if (!processor) {
            if (colorconfig->has_error())
                dst.errorfmt("{}", colorconfig->geterror());
            else
                dst.errorfmt(
                    "Could not construct the color transform (unknown error)");
            return false;
        }
    }

    logtime.stop();  // transition to colorconvert
    bool ok = colorconvert(dst, src, processor.get(), unpremult, roi, nthreads);
    if (ok)
        // If we can parse a color space from the file name, and we're not inverting
        // the transform, then we'll use the color space name from the file.
        // Otherwise, we'll leave `oiio:ColorSpace` alone.
        // TODO: Use OCIO to extract InputDescription and OutputDescription CLF
        // metadata attributes, if present.
        if (!colorconfig->filepathOnlyMatchesDefaultRule(name))
            dst.specmod().set_colorspace(
                colorconfig->getColorSpaceFromFilepath(name));
    return ok;
}



ImageBuf
ImageBufAlgo::ociofiletransform(const ImageBuf& src, string_view name,
                                bool unpremult, bool inverse,
                                const ColorConfig* colorconfig, ROI roi,
                                int nthreads)
{
    ImageBuf result;
    bool ok = ociofiletransform(result, src, name, unpremult, inverse,
                                colorconfig, roi, nthreads);
    if (!ok && !result.has_error())
        result.errorfmt("ImageBufAlgo::ociofiletransform() error");
    return result;
}



bool
ImageBufAlgo::ocionamedtransform(ImageBuf& dst, const ImageBuf& src,
                                 string_view name, bool unpremult, bool inverse,
                                 string_view key, string_view value,
                                 const ColorConfig* colorconfig, ROI roi,
                                 int nthreads)
{
    OIIO::pvt::LoggedTimer logtime("IBA::ocionamedtransform");
    ColorProcessorHandle processor;
    {
        if (!colorconfig)
            colorconfig = &ColorConfig::default_colorconfig();
        processor = colorconfig->createNamedTransform(name, inverse, key,
                                                      value);
        if (!processor) {
            if (colorconfig->has_error())
                dst.errorfmt("{}", colorconfig->geterror());
            else
                dst.errorfmt(
                    "Could not construct the color transform (unknown error)");
            return false;
        }
    }

    logtime.stop();  // transition to colorconvert
    bool ok = colorconvert(dst, src, processor.get(), unpremult, roi, nthreads);
    return ok;
}



ImageBuf
ImageBufAlgo::ocionamedtransform(const ImageBuf& src, string_view name,
                                 bool unpremult, bool inverse, string_view key,
                                 string_view value,
                                 const ColorConfig* colorconfig, ROI roi,
                                 int nthreads)
{
    ImageBuf result;
    bool ok = ocionamedtransform(result, src, name, unpremult, inverse, key,
                                 value, colorconfig, roi, nthreads);
    if (!ok && !result.has_error())
        result.errorfmt("ImageBufAlgo::ocionamedtransform() error");
    return result;
}



bool
ImageBufAlgo::colorconvert(span<float> color, const ColorProcessor* processor,
                           bool unpremult)
{
    // If the processor is NULL, return false (error)
    if (!processor) {
        return false;
    }

    // If the processor is a no-op, no work needs to be done. Early exit.
    if (processor->isNoOp())
        return true;

    // Load the pixel
    float rgba[4]      = { 0.0f, 0.0f, 0.0f, 0.0f };
    int channelsToCopy = std::min(4, (int)color.size());
    memcpy(rgba, color.data(), channelsToCopy * sizeof(float));

    const float fltmin = std::numeric_limits<float>::min();

    // Optionally unpremult
    if ((channelsToCopy >= 4) && unpremult) {
        float alpha = rgba[3];
        if (alpha > fltmin) {
            rgba[0] /= alpha;
            rgba[1] /= alpha;
            rgba[2] /= alpha;
        }
    }

    // Apply the color transformation
    processor->apply(rgba, 1, 1, 4, sizeof(float), 4 * sizeof(float),
                     4 * sizeof(float));

    // Optionally premult
    if ((channelsToCopy >= 4) && unpremult) {
        float alpha = rgba[3];
        if (alpha > fltmin) {
            rgba[0] *= alpha;
            rgba[1] *= alpha;
            rgba[2] *= alpha;
        }
    }

    // Store the scanline
    memcpy(color.data(), rgba, channelsToCopy * sizeof(float));

    return true;
}



void
ColorConfig::set_colorspace(ImageSpec& spec, string_view colorspace) const
{
    // If we're not changing color space, don't mess with anything
    string_view oldspace = spec.get_string_attribute("oiio:ColorSpace");
    if (oldspace.size() && colorspace.size() && oldspace == colorspace)
        return;

    // Set or clear the main "oiio:ColorSpace" attribute
    if (colorspace.empty()) {
        spec.erase_attribute("oiio:ColorSpace");
    } else {
        spec.attribute("oiio:ColorSpace", colorspace);
    }

    // Clear a bunch of other metadata that might contradict the colorspace,
    // including some format-specific things that we don't want to propagate
    // from input to output if we know that color space transformations have
    // occurred.
    if (!equivalent(colorspace, "srgb_rec709_scene"))
        spec.erase_attribute("Exif:ColorSpace");
    spec.erase_attribute("tiff:ColorSpace");
    spec.erase_attribute("tiff:PhotometricInterpretation");
    spec.erase_attribute("oiio:Gamma");
}



void
ColorConfig::set_colorspace_rec709_gamma(ImageSpec& spec, float gamma) const
{
    gamma = std::round(gamma * 100.0f) / 100.0f;
    if (fabsf(gamma - 1.0f) <= 0.01f) {
        set_colorspace(spec, "lin_rec709_scene");
    } else if (fabsf(gamma - 1.8f) <= 0.01f) {
        set_colorspace(spec, "g18_rec709_scene");
        spec.attribute("oiio:Gamma", 1.8f);
    } else if (fabsf(gamma - 2.2f) <= 0.01f) {
        set_colorspace(spec, "g22_rec709_scene");
        spec.attribute("oiio:Gamma", 2.2f);
    } else if (fabsf(gamma - 2.4f) <= 0.01f) {
        set_colorspace(spec, "g24_rec709_scene");
        spec.attribute("oiio:Gamma", 2.4f);
    } else {
        set_colorspace(spec, Strutil::fmt::format("g{}_rec709_scene",
                                                  std::lround(gamma * 10.0f)));
        spec.attribute("oiio:Gamma", gamma);
    }
}


void
set_colorspace(ImageSpec& spec, string_view colorspace)
{
    ColorConfig::default_colorconfig().set_colorspace(spec, colorspace);
}

void
set_colorspace_rec709_gamma(ImageSpec& spec, float gamma)
{
    ColorConfig::default_colorconfig().set_colorspace_rec709_gamma(spec, gamma);
}


OIIO_NAMESPACE_END
