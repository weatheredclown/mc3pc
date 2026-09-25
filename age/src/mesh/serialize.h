#ifndef MESH_SERIALIZE_H
#define MESH_SERIALIZE_H

// Ascii ".mesh" reader.  The original serialiser was symmetric (read/write);
// the port only needs to read the exported files:
//
//   { Skinned 0  PosSkin 0
//     Pos N { x y z ... }  Nrm N { ... }  Cpv N { r g b a ... }
//     Tex0 N { u v ... }  Tex1 N { ... }
//     Adj N { P i [N i] [C0 i] [T0 i] [T1 i] ... }
//     Mtl N { { Name "x" Priority p Prim M { { Type TRIANGLES Priority p Idx K { ... } } ... } } }
//     Offset 0 }
class mshMesh;
class mshSerializer {
public:
    mshSerializer(class datTokenizer &tok, bool writing) : Tok(tok), Writing(writing) {}
    class datTokenizer &Tok;
    bool Writing;
};

// Opens "<name>.mesh" (name may already carry the extension) through ASSET.
bool SerializeFromFile(const char *name, mshMesh &mesh);

#endif // MESH_SERIALIZE_H
