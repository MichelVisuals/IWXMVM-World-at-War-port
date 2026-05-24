#pragma once
#include "Structures.hpp"

namespace IWXMVM::T4::Functions
{
    Structures::dvar_s* FindDvar(const std::string_view name);

    std::string GetFilePath(const std::string_view demoName);

    void Cbuf_AddText(std::string command);

    // T4 MP signature is 4-arg (NOT IW3's 3-arg):
    //   inst = scriptInstance (0 = server-side / default)
    //   string = the interned string
    //   user = entity-class flags (1 for normal lookups)
    //   len = strlen(string) + 1 (includes null terminator)
    uint16_t SL_GetStringOfSize(int inst, const char* string, unsigned int user, unsigned int len);

    bool CG_DObjGetWorldBoneMatrix(Structures::centity_s* entity /*@<eax>*/, int boneIndex /*@<ecx>*/,
                                        float* matrix /*@<esi>*/, Structures::DObj_s* dobj, float* origin);

    Structures::Material* Material_RegisterHandle(const char* materialName);

    void Dvar_SetStringByName(const char* dvarName, const char* value);
}  // namespace IWXMVM::T4::Structures
