////////////////////////////////////////
// material.h
//
// phMaterial base class definitions.
////////////////////////////////////////

#ifndef PHCORE_MATERIAL_H
#define PHCORE_MATERIAL_H

#include "data/token.h"

class datResource;

class phMaterial {
public:
    enum TypeEnum {
        MATERIAL,
        SOUND
    };

    static const char *ClassTypeString;
    // Friction a material carries when nothing has been loaded into it: an
    // ordinary-ground multiplier, not zero.  See material.cpp.
    static const float kDefaultFriction;

    int Type;
    char Name[32];
    float Elasticity;
    float Friction;
    // Effect / sound hook names from the mtl block ("none" in shipped data).
    // Stored so Load -> Save reproduces the source file exactly.
    char Effect[32];
    char SoundName[32];

    phMaterial();
    // From a resource pack image (data/resource.h): vtable, Type, Name[32], Elasticity, Friction (48 bytes).
    phMaterial(datResource &rsc);
    virtual ~phMaterial();

    const char *GetName() const { return Name; }
    void SetName(const char *name) {
        if (name) {
            strncpy(Name, name, sizeof(Name) - 1);
            Name[sizeof(Name) - 1] = 0;
        }
    }
    float GetFriction() const { return Friction; }
    void SetFriction(float f) { Friction = f; }
    float GetElasticity() const { return Elasticity; }
    void SetElasticity(float e) { Elasticity = e; }
    // Copy every property (name included) from another material.
    void Copy(const phMaterial &other) {
        Type = other.Type;
        SetName(other.Name);
        Elasticity = other.Elasticity;
        Friction = other.Friction;
        strncpy(Effect, other.Effect, sizeof(Effect)); Effect[sizeof(Effect) - 1] = 0;
        strncpy(SoundName, other.SoundName, sizeof(SoundName)); SoundName[sizeof(SoundName) - 1] = 0;
    }
    void Copy(const phMaterial *other) { if (other) Copy(*other); }

    virtual int GetClassType() const { return Type; }   // subclasses override to their own id

    // Load/SaveData read and write ONLY this class's fields, in file order, and
    // leave the enclosing "mtl <name> { }" tokens alone -- subclasses chain to
    // them first and then handle their own fields (see rbPhysMaterial).
    // Load/Save handle the full block.  Load(Save(x)) reproduces the canonical
    // file byte-for-byte.
    virtual void LoadData(datAsciiTokenizer &token);
    virtual void SaveData(datAsciiTokenizer &token);
    virtual void Load(datAsciiTokenizer &token);
    virtual void Save(datAsciiTokenizer &token);
};

class phMaterialSnd : public phMaterial {
public:
    static const char *ClassTypeString;
    phMaterialSnd();
    virtual ~phMaterialSnd();
};

#endif // PHCORE_MATERIAL_H
