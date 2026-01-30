// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#include "py_oiio.h"
#include <OpenImageIO/color.h>
#include <optional>
#include <utility>

namespace PyOpenImageIO {

namespace {
py::object decode_utf8_or_bytes(const std::string& s)
{
    PyObject* obj = PyUnicode_DecodeUTF8(s.data(), s.size(),
                                         "surrogateescape");
    if (!obj) {
        PyErr_Clear();
        return py::bytes(s);
    }
    return py::reinterpret_steal<py::str>(obj);
}

std::map<std::string, std::string>
parse_context_vars(const py::dict& context_vars)
{
    std::map<std::string, std::string> out;
    for (auto item : context_vars) {
        std::string key = py::cast<std::string>(item.first);
        if (item.second.is_none()) {
            continue;
        } else if (py::isinstance<py::bytes>(item.second)) {
            out[key] = py::cast<std::string>(item.second);
        } else {
            out[key] = py::cast<std::string>(py::str(item.second));
        }
    }
    return out;
}
}  // namespace


// Declare the OIIO ColorConfig class to Python
void
declare_colorconfig(py::module& m)
{
    using namespace pybind11::literals;

    py::class_<ColorConfig>(m, "ColorConfig")

        .def(py::init<>())
        .def(py::init<const std::string&>())
        .def("geterror",
             [](ColorConfig& self) { return PY_STR(self.geterror()); })

        .def("getNumColorSpaces", &ColorConfig::getNumColorSpaces)
        .def("getColorSpaceNames", &ColorConfig::getColorSpaceNames)
        .def("getColorSpaceNames",
             [](const ColorConfig& self, bool visible, bool hidden, bool scene,
                bool display) {
                 return self.getColorSpaceNamesFiltered(visible, hidden, scene,
                                                        display);
             },
             "visible"_a = true, "hidden"_a = false, "scene"_a = true,
             "display"_a = true)
        .def("getColorSpaces",
             [](const ColorConfig& self, bool visible, bool hidden, bool scene,
                bool display, bool simple) {
                 return self.getColorSpaces(visible, hidden, scene, display,
                                            simple);
             },
             "visible"_a = true, "hidden"_a = false, "scene"_a = true,
             "display"_a = true, "simple"_a = false)
        .def("getDebugInfo",
             [](const ColorConfig& self, bool simple_space_blockers,
                bool cache_stats) {
                 return self.getDebugInfo(simple_space_blockers, cache_stats);
             },
             "simple_space_blockers"_a = false,
             "cache_stats"_a = false)
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
        .def("getName", &ColorConfig::getName)
        .def("getCacheID", &ColorConfig::getCacheID)
        .def("getWorkingDir",
             [](const ColorConfig& self) { return self.getWorkingDir(); })
        .def("setWorkingDir",
             [](ColorConfig& self, const std::string& dir) {
                 self.setWorkingDir(dir);
             })
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
             [](const ColorConfig& self, const std::string& colorspace,
                bool strict) {
                 return std::string(
                     self.get_color_interop_id(colorspace, strict));
             },
             "colorspace"_a, "strict"_a = false)
        .def("get_color_interop_id",
             [](const ColorConfig& self, const std::string& colorspace,
                bool strict, const py::dict& context) {
                 auto parsed = parse_context_vars(context);
                 return self.get_color_interop_id(colorspace, strict, parsed);
             },
             "colorspace"_a, "strict"_a = false, "context"_a = py::dict())
        .def("get_color_interop_id",
             [](const ColorConfig& self, const std::array<int, 4> cicp) {
                 return std::string(self.get_color_interop_id(cicp.data()));
             })
        .def("get_equality_ids",
             [](const ColorConfig& self, bool exhaustive,
                const py::dict& context) {
                 auto parsed = parse_context_vars(context);
                 auto result
                     = self.get_equality_ids(exhaustive,
                                                                   parsed);
                 py::dict out;
                 for (const auto& kv : result) {
                     out[decode_utf8_or_bytes(kv.first)]
                         = decode_utf8_or_bytes(kv.second);
                 }
                 return out;
             },
             "exhaustive"_a = false, "context"_a = py::dict())
        .def("get_interop_ids",
             [](const ColorConfig& self, bool strict, bool exhaustive,
                const py::dict& context) {
                 auto parsed = parse_context_vars(context);
                 auto result = self.get_interop_ids(
                     strict, exhaustive, parsed);
                 py::dict out;
                 for (const auto& kv : result) {
                     out[decode_utf8_or_bytes(kv.first)]
                         = decode_utf8_or_bytes(kv.second);
                 }
                 return out;
             },
             "strict"_a = false, "exhaustive"_a = false,
             "context"_a = py::dict())
        .def("get_colorspace_fingerprint",
             [](const ColorConfig& self, const std::string& colorspace,
                const py::dict& context) {
                 auto parsed = parse_context_vars(context);
                 return self.get_colorspace_fingerprint(colorspace,
                                                        parsed);
             },
             "colorspace"_a,
             "context"_a = py::dict())
        .def("find_colorspace_from_fingerprint",
             [](const ColorConfig& self, const std::vector<float>& fingerprint,
                bool display_referred,
                const py::dict& context) {
                 auto parsed = parse_context_vars(context);
                 return self.find_colorspace_from_fingerprint(
                     fingerprint, display_referred, parsed);
             },
             "fingerprint"_a, "display_referred"_a = false,
             "context"_a = py::dict())
        .def("get_intersection",
             [](const ColorConfig& self, const ColorConfig& other,
                const py::dict& base_context,
                const py::dict& other_context) {
                 auto base_parsed = parse_context_vars(base_context);
                 auto other_parsed = parse_context_vars(other_context);
                 auto result = self.get_intersection(
                     other, base_parsed, other_parsed);
                 py::list out;
                 for (const auto& pair : result) {
                     out.append(py::make_tuple(decode_utf8_or_bytes(pair.first),
                                               decode_utf8_or_bytes(pair.second)));
                 }
                 return out;
             },
             "other"_a, "base_context"_a = py::dict(),
             "other_context"_a = py::dict())
        .def("match_fingerprint_to_colorspace",
             [](const ColorConfig& self, const std::vector<float>& fingerprint,
                bool display_cs, const py::dict& context) {
                 auto parsed = parse_context_vars(context);
                 return self.find_colorspace_from_fingerprint(
                     fingerprint, display_cs, parsed);
             },
             "fingerprint"_a, "display_cs"_a = false,
             "context"_a = py::dict())
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
        .def("configname", &ColorConfig::configname)
        .def_static("default_colorconfig", []() -> const ColorConfig& {
            return ColorConfig::default_colorconfig();
        });

    m.attr("supportsOpenColorIO")     = ColorConfig::supportsOpenColorIO();
    m.attr("OpenColorIO_version_hex") = ColorConfig::OpenColorIO_version_hex();
}

}  // namespace PyOpenImageIO
