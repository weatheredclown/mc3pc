#ifndef GFX_RV1MEM_H
#define GFX_RV1MEM_H

// VU1 memory layout / addresses
#define rv1_AMEM_MATERIAL_CLR 0
#define rv1_BIND_NEXT_STATE 0
#define rv1SKIN_MTX_BUFFER 0
#define rv1SKIN_MTX_BUFFER_MTX_SIZE 64
#define rv1_AMEM_NEXT_RENDER_STATE 0
#define rv1_AMEM_TEXTURE_MTX 0
#define rv1_AMEM_SCRATCH_MTX 0

// Render state flags
#define rv1_RSTATE_CULL_MASK 0x03
#define rv1_RSTATE_CW_CULL 0x01
#define rv1_RSTATE_CCW_CULL 0x02
#define rv1_RSTATE_FOG 0x04
#define rv1_RSTATE_LIGHT 0x08

// Texture coordinate preprocess source modes
#define rv1_PREPROCESS_TEX_SRC_NORMAL 0
#define rv1_PREPROCESS_TEX_SRC_POSITION 1
#define rv1_PREPROCESS_TEX_SRC_REFLECT 2
#define rv1_PREPROCESS_TEX_SRC_SPHEREMAP 3

#endif // GFX_RV1MEM_H
