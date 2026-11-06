/*
 * xbintsc GUI engine — WGSL entry-point wrappers.
 *
 * A WGSL entry point is an ordinary function whose parameters and return value
 * carry `@location`/`@builtin` attributes. GLSL has no equivalent, so the
 * translator emits the entry point as a regular function and a `main` wrapper
 * that declares the `in`/`out` globals, calls it with the builtins and copies
 * the returned struct's members to `gl_Position`/`gl_FragDepth` or to the
 * location-qualified outputs.
 */

#include <string>
#include <vector>

#include "wgsl_emit.h"

namespace xtgui {
namespace wgsl {
namespace {

bool integer_scalar(Scalar scalar) {
  return scalar == Scalar::I32 || scalar == Scalar::U32;
}

std::string location_declare(const Type &type, const std::string &name, int location, bool output,
                             const std::string &interpolate, bool flat) {
  std::string qualifier = "layout(location = " + std::to_string(location) + ") ";
  if (flat || interpolate == "flat") qualifier += "flat ";
  else if (interpolate == "linear") qualifier += "noperspective ";
  else if (interpolate == "perspective") qualifier += "smooth ";
  qualifier += output ? "out " : "in ";
  return qualifier + glsl_decl(type, name) + ";\n";
}

}  // namespace

std::string Emitter::builtin_variable(const std::string &name, Stage stage) const {
  if (stage == Stage::Vertex) {
    /* Vulkan GLSL spells them …Index (the OpenGL …ID aliases are rejected). */
    if (name == "vertex_index") return "gl_VertexIndex";
    if (name == "instance_index") return "gl_InstanceIndex";
  }
  if (stage == Stage::Fragment) {
    if (name == "position") return "gl_FragCoord";
    if (name == "front_facing") return "gl_FrontFacing";
    if (name == "sample_index") return "gl_SampleID";
    if (name == "sample_mask") return "gl_SampleMaskIn[0]";
    if (name == "primitive_index") return "gl_PrimitiveID";
  }
  if (stage == Stage::Compute) {
    if (name == "local_invocation_id") return "gl_LocalInvocationID";
    if (name == "local_invocation_index") return "gl_LocalInvocationIndex";
    if (name == "global_invocation_id") return "gl_GlobalInvocationID";
    if (name == "workgroup_id") return "gl_WorkGroupID";
    if (name == "num_workgroups") return "gl_NumWorkGroups";
    if (name == "subgroup_invocation_id") return "gl_SubgroupInvocationID";
    if (name == "subgroup_size") return "gl_SubgroupSize";
  }
  return std::string();
}

bool Emitter::emit_entry(const FuncDecl &entry, std::string *text) {
  std::string inputs;
  std::string call_args;
  int input_index = 0;
  for (const Param &param : entry.params) {
    if (!param.builtin.empty()) {
      std::string value = builtin_variable(param.builtin, stage_);
      if (value.empty()) {
        fail("unsupported builtin '@" + param.builtin + "' in entry point '" + entry.name + "'");
        return false;
      }
      if (param.type != nullptr && param.type->kind == Kind::Scalar &&
          param.type->scalar == Scalar::U32 && param.builtin != "local_invocation_id" &&
          param.builtin != "global_invocation_id" && param.builtin != "workgroup_id" &&
          param.builtin != "num_workgroups" && param.builtin != "local_invocation_index") {
        value = "uint(" + value + ")";
      }
      if (!call_args.empty()) call_args += ", ";
      call_args += value;
      continue;
    }
    if (param.type != nullptr && param.type->kind == Kind::Struct && param.type->strct != nullptr) {
      std::string constructor;
      for (const Member &member : param.type->strct->members) {
        if (!member.builtin.empty()) {
          std::string value = builtin_variable(member.builtin, stage_);
          if (value.empty()) {
            fail("unsupported input builtin '@" + member.builtin + "'");
            return false;
          }
          if (!constructor.empty()) constructor += ", ";
          constructor += value;
          continue;
        }
        if (member.location < 0 || member.type == nullptr) continue;
        const std::string name = "xg_loc_in_" + std::to_string(input_index++);
        inputs += location_declare(*member.type, name, member.location, false, member.interpolate,
                                   member.type->kind == Kind::Scalar && integer_scalar(member.type->scalar));
        if (!constructor.empty()) constructor += ", ";
        constructor += name;
      }
      if (!call_args.empty()) call_args += ", ";
      call_args += param.type->strct->name + "(" + constructor + ")";
      continue;
    }
    if (param.location >= 0 && param.type != nullptr) {
      const std::string name = "xg_loc_in_" + std::to_string(input_index++);
      inputs += location_declare(*param.type, name, param.location, false, param.interpolate,
                                 param.type->kind == Kind::Scalar && integer_scalar(param.type->scalar));
      if (!call_args.empty()) call_args += ", ";
      call_args += name;
      continue;
    }
    fail("entry point parameter '" + param.name + "' needs @location or @builtin");
    return false;
  }

  std::string outputs;
  std::string assignments;
  int output_index = 0;
  if (entry.ret != nullptr && entry.ret->kind != Kind::Void) {
    std::string result = "xg_result";
    if (entry.ret->kind == Kind::Struct && entry.ret->strct != nullptr) {
      for (const Member &member : entry.ret->strct->members) {
        if (member.type == nullptr) continue;
        const std::string value = result + "." + member_name(member.name);
        if (!member.builtin.empty()) {
          if (member.builtin == "position") assignments += "  gl_Position = " + value + ";\n";
          else if (member.builtin == "frag_depth") assignments += "  gl_FragDepth = " + value + ";\n";
          else if (member.builtin == "sample_mask") assignments += "  gl_SampleMask[0] = int(" + value + ");\n";
          else {
            fail("unsupported entry-point output builtin '@" + member.builtin + "'");
            return false;
          }
          continue;
        }
        if (member.location >= 0) {
          const std::string name = "xg_loc_out_" + std::to_string(output_index++);
          outputs += location_declare(*member.type, name, member.location, true, member.interpolate,
                                      member.type->kind == Kind::Scalar &&
                                          integer_scalar(member.type->scalar));
          assignments += "  " + name + " = " + value + ";\n";
        }
      }
    } else if (!entry.ret_builtin.empty()) {
      if (entry.ret_builtin == "position") assignments += "  gl_Position = " + result + ";\n";
      else if (entry.ret_builtin == "frag_depth") assignments += "  gl_FragDepth = " + result + ";\n";
      else {
        fail("unsupported entry-point output builtin '@" + entry.ret_builtin + "'");
        return false;
      }
    } else if (entry.ret_location >= 0) {
      const std::string name = "xg_loc_out_0";
      outputs += location_declare(*entry.ret, name, entry.ret_location, true, entry.ret_interpolate,
                                  entry.ret->kind == Kind::Scalar &&
                                      integer_scalar(entry.ret->scalar));
      assignments += "  " + name + " = " + result + ";\n";
    }
  }

  std::string head;
  if (stage_ == Stage::Compute) {
    head = "layout(local_size_x = " + std::to_string(entry.workgroup[0]) +
           ", local_size_y = " + std::to_string(entry.workgroup[1]) +
           ", local_size_z = " + std::to_string(entry.workgroup[2]) + ") in;\n";
    out_->workgroup_size[0] = entry.workgroup[0];
    out_->workgroup_size[1] = entry.workgroup[1];
    out_->workgroup_size[2] = entry.workgroup[2];
  }

  std::string definition = emit_user_function(entry);
  std::string wrapper = "void main() {\n";
  const bool returns_value = entry.ret != nullptr && entry.ret->kind != Kind::Void;
  if (returns_value) {
    wrapper += "  " + glsl_decl(*entry.ret, "xg_result") + " = xg_" + entry.name + "(" +
               call_args + ");\n";
  } else {
    wrapper += "  xg_" + entry.name + "(" + call_args + ");\n";
  }
  wrapper += assignments;
  wrapper += "}\n";

  *text = head + inputs + outputs + definition + wrapper;
  return true;
}

}  // namespace wgsl
}  // namespace xtgui
