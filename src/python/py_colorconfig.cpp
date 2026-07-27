// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#include "py_oiio.h"
#include <OpenImageIO/color.h>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace PyOpenImageIO {


// Declare the OIIO ColorConfig class to Python
void
declare_colorconfig(py_module& m)
{
    py::enum_<ColorSpaceInfoField>(m, "ColorSpaceInfoField")
        .value("EqualityID", ColorSpaceInfoField::EqualityID)
        .value("ColorInteropID", ColorSpaceInfoField::ColorInteropID)
        .value("Encoding", ColorSpaceInfoField::Encoding)
        .value("ImageState", ColorSpaceInfoField::ImageState)
        .value("Range", ColorSpaceInfoField::Range)
        .value("Chromaticities", ColorSpaceInfoField::Chromaticities)
        .value("TransferFunction", ColorSpaceInfoField::TransferFunction);

    py::enum_<ColorTransferFunctionKind>(m, "ColorTransferFunctionKind")
        .value("Undetermined", ColorTransferFunctionKind::Undetermined)
        .value("Linear", ColorTransferFunctionKind::Linear)
        .value("Named", ColorTransferFunctionKind::Named)
        .value("Sampled", ColorTransferFunctionKind::Sampled);

    // A helper for the optional string properties: an unavailable field
    // maps to None; empty strings are not used to erase the difference
    // between "unavailable" and a legitimate empty value.
    auto opt_field = [](const ColorSpaceInfo& self, ColorSpaceInfoField field,
                        string_view value) -> std::optional<std::string> {
        if (!self.available(field))
            return std::nullopt;
        return std::string(value);
    };

    py::class_<ColorSpaceInfo>(m, "ColorSpaceInfo")
        .OIIO_PY_PROP_RO("name",
                         [](const ColorSpaceInfo& self) {
                             return std::string(self.name());
                         })
        .OIIO_PY_PROP_RO("equality_id",
                         [opt_field](const ColorSpaceInfo& self) {
                             return opt_field(self,
                                              ColorSpaceInfoField::EqualityID,
                                              self.equality_id());
                         })
        .OIIO_PY_PROP_RO("color_interop_id",
                         [opt_field](const ColorSpaceInfo& self) {
                             return opt_field(
                                 self, ColorSpaceInfoField::ColorInteropID,
                                 self.color_interop_id());
                         })
        .OIIO_PY_PROP_RO("encoding",
                         [opt_field](const ColorSpaceInfo& self) {
                             return opt_field(self,
                                              ColorSpaceInfoField::Encoding,
                                              self.encoding());
                         })
        .OIIO_PY_PROP_RO("image_state",
                         [opt_field](const ColorSpaceInfo& self) {
                             return opt_field(self,
                                              ColorSpaceInfoField::ImageState,
                                              self.image_state());
                         })
        .OIIO_PY_PROP_RO("range",
                         [opt_field](const ColorSpaceInfo& self) {
                             return opt_field(self, ColorSpaceInfoField::Range,
                                              self.range());
                         })
        .OIIO_PY_PROP_RO("chromaticities",
                         [](const ColorSpaceInfo& self) -> py::object {
                             cspan<float> c = self.chromaticities();
                             if (c.size() != 8)
                                 return py::none();
                             return oiio_py::make_tuple(8, [&](size_t i) {
                                 return py::float_(c[i]);
                             });
                         })
        .OIIO_PY_PROP_RO("transfer_function_kind",
                         [](const ColorSpaceInfo& self) {
                             return self.transfer_function_kind();
                         })
        .OIIO_PY_PROP_RO("transfer_function",
                         [](const ColorSpaceInfo& self)
                             -> std::optional<std::string> {
                             string_view family = self.transfer_function();
                             if (family.empty())
                                 return std::nullopt;
                             return std::string(family);
                         })
        .def(
            "computed",
            [](const ColorSpaceInfo& self, ColorSpaceInfoField field) {
                return self.computed(field);
            },
            "field"_a)
        .def(
            "available",
            [](const ColorSpaceInfo& self, ColorSpaceInfoField field) {
                return self.available(field);
            },
            "field"_a)
        .def(
            "derived",
            [](const ColorSpaceInfo& self, ColorSpaceInfoField field) {
                return self.derived(field);
            },
            "field"_a);

    py::class_<ColorConfig>(m, "ColorConfig")

        .def(py::init<>())
        .def(py::init<const std::string&>())
        .def("geterror",
             [](ColorConfig& self) { return PY_STR(self.geterror()); })

        .def("getNumColorSpaces", &ColorConfig::getNumColorSpaces)
        .def("getColorSpaceNames", &ColorConfig::getColorSpaceNames)
        .def("getColorSpaceNameByIndex", &ColorConfig::getColorSpaceNameByIndex)
        .def(
            "getColorSpaceIndex",
            [](const ColorConfig& self, const std::string& name) {
                return self.getColorSpaceIndex(name);
            },
            "name"_a)
        .def(
            "getColorSpaceNameByRole",
            [](const ColorConfig& self, const std::string& role) {
                return self.getColorSpaceNameByRole(role);
            },
            "role"_a)
        .def("getNumRoles", &ColorConfig::getNumRoles)
        .def("getRoleByIndex", &ColorConfig::getRoleByIndex)
        .def("getRoles", &ColorConfig::getRoles)
        .def(
            "getColorSpaceDataType",
            [](const ColorConfig& self, const std::string& name) {
                int bits      = 0;
                TypeDesc type = self.getColorSpaceDataType(name, &bits);
                return std::make_pair(type, bits);
            },
            "name"_a)
        .def(
            "getColorSpaceFamilyByName",
            [](const ColorConfig& self, const std::string& name) {
                return self.getColorSpaceFamilyByName(name);
            },
            "name"_a)

        .def("getNumLooks", &ColorConfig::getNumLooks)
        .def("getLookNameByIndex", &ColorConfig::getLookNameByIndex)
        .def("getLookNames", &ColorConfig::getLookNames)

        .def("getNumDisplays", &ColorConfig::getNumDisplays)
        .def("getDisplayNameByIndex", &ColorConfig::getDisplayNameByIndex)
        .def("getDisplayNames", &ColorConfig::getDisplayNames)
        .def("getDefaultDisplayName", &ColorConfig::getDefaultDisplayName)

        .def(
            "getNumViews",
            [](const ColorConfig& self, const std::string& display) {
                return self.getNumViews(display);
            },
            "display"_a = "")
        .def(
            "getViewNameByIndex",
            [](const ColorConfig& self, const std::string& display, int index) {
                return self.getViewNameByIndex(display, index);
            },
            "display"_a = "", "index"_a)
        .def(
            "getViewNames",
            [](const ColorConfig& self, const std::string& display) {
                return self.getViewNames(display);
            },
            "display"_a = "")
        .def(
            "getDefaultViewName",
            [](const ColorConfig& self, const std::string& display) {
                return self.getDefaultViewName(display);
            },
            "display"_a = "")
        .def(
            "getDefaultViewName",
            [](const ColorConfig& self, const std::string& display,
               const std::string& input_color_space) {
                return self.getDefaultViewName(display, input_color_space);
            },
            "display"_a = "", "input_color_space"_a)
        .def(
            "getDisplayViewColorSpaceName",
            [](const ColorConfig& self, const std::string& display,
               const std::string& view) {
                return self.getDisplayViewColorSpaceName(display, view);
            },
            "display"_a, "view"_a)
        .def(
            "getDisplayViewLooks",
            [](const ColorConfig& self, const std::string& display,
               const std::string& view) {
                return self.getDisplayViewLooks(display, view);
            },
            "display"_a, "view"_a)

        .def("getAliases",
             [](const ColorConfig& self, const std::string& color_space) {
                 return self.getAliases(color_space);
             })
        .def("getNumNamedTransforms", &ColorConfig::getNumNamedTransforms)
        .def("getNamedTransformNameByIndex",
             &ColorConfig::getNamedTransformNameByIndex)
        .def("getNamedTransformNames", &ColorConfig::getNamedTransformNames)
        .def("getNamedTransformAliases",
             [](const ColorConfig& self, const std::string& named_transform) {
                 return self.getNamedTransformAliases(named_transform);
             })
        .def(
            "isColorSpaceLinear",
            [](const ColorConfig& self, const std::string& name) {
                return self.isColorSpaceLinear(name);
            },
            "name"_a)
        .def(
            "isData",
            [](const ColorConfig& self, const std::string& name) {
                return self.isData(name);
            },
            "name"_a)
        .def(
            "getColorSpaceFromFilepath",
            [](const ColorConfig& self, const std::string& filepath) {
                return std::string(self.getColorSpaceFromFilepath(filepath));
            },
            "filepath"_a)
        .def(
            "getColorSpaceFromFilepath",
            [](const ColorConfig& self, const std::string& filepath,
               const std::string& default_cs, const bool& cs_name_match) {
                return std::string(
                    self.getColorSpaceFromFilepath(filepath, default_cs,
                                                   cs_name_match));
            },
            "filepath"_a, "default_cs"_a, "cs_name_match"_a = false)
        .def(
            "filepathOnlyMatchesDefaultRule",
            [](const ColorConfig& self, const std::string& filepath) {
                return self.filepathOnlyMatchesDefaultRule(filepath);
            },
            "filepath"_a)
        .def("parseColorSpaceFromString",
             [](const ColorConfig& self, const std::string& str) {
                 return std::string(self.parseColorSpaceFromString(str));
             })
        .def(
            "resolve",
            [](const ColorConfig& self, const std::string& name) {
                return std::string(self.resolve(name));
            },
            "name"_a)
        .def(
            "equivalent",
            [](const ColorConfig& self, const std::string& color_space,
               const std::string& other_color_space) {
                return self.equivalent(color_space, other_color_space);
            },
            "color_space"_a, "other_color_space"_a)
        .def("get_color_interop_id",
             [](const ColorConfig& self, const std::string& colorspace) {
                 return std::string(self.get_color_interop_id(colorspace));
             })
        .def("get_color_interop_id",
             [](const ColorConfig& self, const std::array<int, 4> cicp) {
                 return std::string(self.get_color_interop_id(cicp.data()));
             })
        .def("get_cicp",
             [](const ColorConfig& self, const std::string& colorspace)
                 -> std::optional<std::array<int, 4>> {
                 cspan<int> cicp = self.get_cicp(colorspace);
                 if (!cicp.empty()) {
                     return std::array<int, 4>(
                         { cicp[0], cicp[1], cicp[2], cicp[3] });
                 }
                 return std::nullopt;
             })
        .def(
            "get_color_space_info",
            [](const ColorConfig& self, const std::string& name,
               const std::map<std::string, std::string>& context_vars,
               const std::string& profile,
               const std::string& policies) -> std::optional<ColorSpaceInfo> {
                ColorSpaceInfoOptions opts;
                opts.context        = context_vars;
                opts.profile        = profile;   // reserved, currently ignored
                opts.policies       = policies;  // reserved, currently ignored
                ColorSpaceInfo info = self.get_color_space_info(name, opts);
                // Invalid input maps to None; the error stays on the
                // ColorConfig (geterror()).
                if (!info.valid())
                    return std::nullopt;
                return info;
            },
            "name"_a, py::kw_only(),
            "context_vars"_a = std::map<std::string, std::string>(),
            "profile"_a = "", "policies"_a = "")
        .def(
            "get_color_space_infos",
            [](const ColorConfig& self, const std::vector<std::string>& names,
               const std::map<std::string, std::string>& context_vars,
               const std::string& profile, const std::string& policies) {
                ColorSpaceInfoOptions opts;
                opts.context  = context_vars;
                opts.profile  = profile;   // reserved, currently ignored
                opts.policies = policies;  // reserved, currently ignored
                std::vector<ColorSpaceInfo> infos;
                {
                    // Pure C++ work, no Python objects: release the GIL for
                    // the batch (invalid batch input returns [] and leaves
                    // the error on the ColorConfig).
                    py::gil_scoped_release gil;
                    infos = self.get_color_space_infos(names, opts);
                }
                return infos;
            },
            "names"_a, py::kw_only(),
            "context_vars"_a = std::map<std::string, std::string>(),
            "profile"_a = "", "policies"_a = "")
        .def("configname", &ColorConfig::configname)
        .def_static(
            "default_colorconfig",
            []() -> const ColorConfig& {
                return ColorConfig::default_colorconfig();
            },
            oiio_py::ref);

    m.attr("supportsOpenColorIO")     = ColorConfig::supportsOpenColorIO();
    m.attr("OpenColorIO_version_hex") = ColorConfig::OpenColorIO_version_hex();
}

}  // namespace PyOpenImageIO
