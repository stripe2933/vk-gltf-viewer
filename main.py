from collections import defaultdict
from itertools import chain
import json
import os
import re

with open("cimgui/imgui/imgui.h", "r") as file:
    imgui_h = file.read()
with open("cimgui/imgui/imgui_internal.h", "r") as file:
    imgui_internal_h = file.read()
with open("cimgui/imgui/misc/freetype/imgui_freetype.h", "r") as file:
    imgui_freetype_h = file.read()

def get_symbol_location(symbol: str) -> str:
    if symbol in imgui_h:
        return "imgui"
    elif symbol in imgui_internal_h:
        return "imgui_internal"
    elif symbol in imgui_freetype_h:
        return "imgui_freetype"
    else:
        raise ValueError(f"Symbol {symbol} not found in any header file.")

class OutputDict(defaultdict):
    def __missing__(self, key):
        self[key] = f"module;\n\n#include <{key}.h>\n\nexport module {key};\n\n"

        if key == "imgui":
            # cimgui does not handle math operators at all, so we need to manually export them.
            self[key] += "export {\n#ifdef IMGUI_DEFINE_MATH_OPERATORS\n    using ::operator+;\n    using ::operator-;\n    using ::operator*;\n    using ::operator/;\n    using ::operator+=;\n    using ::operator-=;\n    using ::operator*=;\n    using ::operator/=;\n    using ::operator==;\n    using ::operator!=;\n#endif\n\n"
        else:
            self[key] += "export import imgui;\n\nexport {\n"

        return self[key]

outputs = OutputDict()

# ----- Process enums and structs -----

with open("cimgui/generator/output/structs_and_enums.json", "r") as file:
    data = json.load(file)

    symbol_locations: dict[str, str] = {symbol: location.split(":")[0] for symbol, location in data["locations"].items()}

    # ---- Process enums ----

    enums: defaultdict[str, list[tuple[str, list[str]]]] = defaultdict(list)
    for prefix, flag_infos in data["enums"].items():
        enums[symbol_locations[prefix]].append((prefix, [info["name"] for info in flag_infos]))

    for location, enums in enums.items():
        outputs[location] += "    // ----- Enums -----\n"
        for prefix, flags in enums:
            outputs[location] += f"\n    using ::{prefix};\n"
            for flag in flags:
                outputs[location] += f"    using ::{flag};\n"   

    # ----- Process structs -----

    # Hardcoded structs that are nested inside other structs, have to be handled separately.
    predefined_nested_structs = {
        "ImGuiTextRange"
    }

    structs: defaultdict[str, set[str]] = defaultdict(set)

    for struct_name in data["structs"]:
        if struct_name in predefined_nested_structs:
            continue # Will be implicitly exported by the parent struct.

        structs[symbol_locations[struct_name]].add(struct_name)

    for struct_name in data["templated_structs"]:
        if struct_name in predefined_nested_structs:
            continue # Will be implicitly exported by the parent struct.

        structs[get_symbol_location(struct_name)].add(struct_name)

    for location, structs in structs.items():
        outputs[location] += "\n    // ----- Structs -----\n\n"
        for struct in structs:
            outputs[location] += f"    using ::{struct};\n"

# ----- Process alias types -----

with open("cimgui/generator/output/typedefs_dict.json", "r") as file:
    data = json.load(file)

    aliases: defaultdict[str, list[str]] = defaultdict(list)
    for alias, original in data.items():
        if f"struct {alias}" == original:
            continue # Already handled by the structs processing above.

        aliases[get_symbol_location(alias)].append(alias)

    for location, alias_list in aliases.items():
        outputs[location] += "\n    // ----- Type aliases -----\n\n"
        for alias in alias_list:
            outputs[location] += f"    using ::{alias};\n"

# ----- Process functions -----

with open("cimgui/generator/output/definitions.json", "r") as file:
    data = json.load(file)

    # Hardcoded functions that are called as ImGui::XXX(), but starts with "Im".
    # Currently cimgui does not export whether the function is in the ImGui 
    # namespace or not, therefore for now it is determined by the heuristic, by
    # checking if the function name starts with "Im". These are exceptions to 
    # the heuristic, which are in the ImGui namespace, but start with "Im".
    predefined_namespace_funcs = {
        "ImageWithBg",
        "Image",
        "ImageButton",
        "ImageButtonEx",
    }

    funcs_in_namespace: defaultdict[str, set[str]] = defaultdict(set) # Functions that are called as ImGui::XXX().
    funcs: defaultdict[str, set[str]] = defaultdict(set)
    for definition in chain.from_iterable(data.values()):
        if definition["stname"]:
            # Definition is a method of a struct, which will be implicitly exported by the struct.
            pass
        else:
            # Definition is a free function.
            location = definition["location"].split(":")[0]
            function_name = definition["funcname"]
            if function_name in predefined_namespace_funcs or not function_name.startswith("Im"):
                funcs_in_namespace[location].add(definition["funcname"])
            else:
                funcs[location].add(function_name)

    output_locations = set(funcs.keys()).union(funcs_in_namespace.keys())
    for location in output_locations:
        outputs[location] += "\n    // ----- Functions -----\n\n"

    for location, function_names in funcs.items():
        for function_name in function_names:
            outputs[location] += f"    using ::{function_name};\n"
        outputs[location] += "\n"

    for location, function_names in funcs_in_namespace.items():
        outputs[location] += "namespace ImGui {\n"
        for function_name in function_names:
            outputs[location] += f"    using ImGui::{function_name};\n"

        if location == "imgui":
            # IMGUI_CHECKVERSION() is a macro that cannot be exported by module.
            # For workaround, we define a function that calls the macro.
            outputs[location] += "\n    /**\n     * @brief Use this for the replacement of <tt>IMGUI_CHECKVERSION()</tt>.\n     */\n    void CheckVersion() { IMGUI_CHECKVERSION(); };\n"

        outputs[location] += "}\n"

for location in outputs:
    outputs[location] += "}\n"

for location, output in outputs.items():
    os.makedirs("generated", exist_ok=True)
    with open(f"generated/{location}.cppm", "w") as file:
        file.write(output)

# ----- Process backends -----

outputs.clear()


with open("cimgui/generator/output/impl_definitions.json", "r") as file:
    data = json.load(file)

    # cimgui does not export the type names of the impl header, therefore we need
    # to extract them from function type arguments.
    types: defaultdict[str, set[str]] = defaultdict(set)
    type_identifier_re = re.compile(r"(?:const\s+)?(?:struct\s+)?([a-zA-Z_][a-zA-Z0-9_]*)\*?")

    # Built-in primitive types must be excluded from export.
    primitive_types = {
        "bool", "int", "float", "double", "void", "char", "unsigned char",
        "short", "unsigned short", "long", "unsigned long", "long long",
        "unsigned long long", "size_t", "uint8_t", "uint16_t", "uint32_t",
        "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t", "unsigned"
    }
    
    funcs: defaultdict[str, set[str]] = defaultdict(set)
    for definition in chain.from_iterable(data.values()):
        location = definition["location"].split(":")[0]
        
        for argument_info in definition["argsT"]:
            typename = type_identifier_re.match(argument_info["type"]).group(1)
            if typename not in primitive_types:
                types[location].add(typename)

        if definition["stname"]:
            # Definition is a method of a struct, which will be implicitly exported by the struct.
            pass
        else:
            funcs[location].add(definition["funcname"])

    for location, types in types.items():
        outputs[location] += "    // ----- Types -----\n\n"
        for type in types:
            outputs[location] += f"    using ::{type};\n"
        outputs[location] += "\n"

    for location, function_names in funcs.items():
        outputs[location] += "    // ----- Functions -----\n\n"
        for function_name in function_names:
            outputs[location] += f"    using ::{function_name};\n"

    for location in outputs:
        outputs[location] += "}\n"

for location, output in outputs.items():
    os.makedirs("generated/backends", exist_ok=True)
    with open(f"generated/backends/{location}.cppm", "w") as file:
        file.write(output)