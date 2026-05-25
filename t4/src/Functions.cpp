#include "StdInclude.hpp"
#include "Functions.hpp"

#include "Addresses.hpp"

namespace IWXMVM::T4::Functions
{
    Structures::dvar_s* FindDvar(const std::string_view name)
    {
        // T4 MP Dvar_FindVar = 0x005C4040, standard __cdecl with name as the
        // first (and only) stack argument. Verified by disassembling the
        // running game's memory dump:
        //   push esi; push edi; <spinlock>; mov edi, [esp+0xC]; ...
        // The 0xC offset matches a __cdecl call with 4-byte ret addr +
        // pushed esi + pushed edi = 12 bytes of preceding stack.
        typedef Structures::dvar_s*(__cdecl * Dvar_FindVar_t)(const char* name);
        auto Dvar_FindVar_Internal = (Dvar_FindVar_t)GetGameAddresses().Dvar_FindMalleableVar();
        return Dvar_FindVar_Internal(name.data());
    }

    std::string GetFilePath(const std::string_view demoName)
    {
        auto searchpath = (Structures::searchpath_s*)GetGameAddresses().fs_searchpaths();
        while (searchpath->next)
        {
            searchpath = searchpath->next;
            if (!searchpath->dir)
                continue;

            auto path = std::filesystem::path(searchpath->dir->path);
            path.append(searchpath->dir->gamedir);
            path.append("demos");
            path.append(demoName);

            if (std::filesystem::exists(path))
                return path.string();
        }

        return "";
    }

    // TODO: now this should really not belong in a file called "Structures.cpp"...
    void Cbuf_AddText(std::string command)
    {
        LOG_DEBUG("Executing command \"{0}\"", command);

        command.append("\n");

        const char* commandString = command.c_str();
        const auto Cbuf_AddText_Address = GetGameAddresses().Cbuf_AddText();

        __asm
        {
            mov eax, commandString
            mov ecx, 0
            call Cbuf_AddText_Address
        }
    }

    uint16_t SL_GetStringOfSize(int inst, const char* string, unsigned int user, unsigned int len)
    {
        typedef uint16_t(__cdecl * SL_GetStringOfSize_t)(int, const char*, unsigned int, unsigned int);
        SL_GetStringOfSize_t SL_GetStringOfSize = (SL_GetStringOfSize_t)GetGameAddresses().SL_GetStringOfSize();

        return SL_GetStringOfSize(inst, string, user, len);
    }

    
    bool CG_DObjGetWorldBoneMatrix(Structures::centity_s* entity /*@<eax>*/, int boneIndex /*@<ecx>*/,
                                   float* matrix /*@<esi>*/, Structures::DObj_s* dobj, float* origin)
    {
        // T4 MP calling convention — same ABI as iw3 (shipping for years):
        //   entity@<eax>, boneIndex@<ecx>, matrix@<esi>, then cdecl stack:
        //   push origin, push dobj. (NOT t4-rtx's SP ABI which uses edi for
        //   obj and stack for axis — different function, different layout.)
        // pushad/popad preserves all caller registers across the call;
        // result EAX captured to a static before popad restores it.
        static uintptr_t address = GetGameAddresses().CG_DObjGetWorldBoneMatrix();
        static int result_byte = 0;
        __asm
        {
            pushad
            mov eax, entity
            mov ecx, boneIndex
            mov esi, matrix
            push origin
            push dobj
            call address
            add esp, 8
            mov result_byte, eax
            popad
        }
        return result_byte != 0;
    }


    Structures::Material* Material_RegisterHandle(const char* materialName)
    {
        typedef Structures::Material*(__cdecl * Material_RegisterHandle_t)(const char* materialName, int a2);
        Material_RegisterHandle_t Material_RegisterHandle =
            reinterpret_cast<Material_RegisterHandle_t>(GetGameAddresses().Material_RegisterHandle());

        return Material_RegisterHandle(materialName, 3);
    }

    void Dvar_SetStringByName(const char* dvarName, const char* value)
    {
        static uintptr_t address = GetGameAddresses().Dvar_SetStringByName();
        __asm 
        {
            mov eax, dvarName
            push value
            call address
            add esp, 4
        }
    }

}  // namespace IWXMVM::T4::Structures
