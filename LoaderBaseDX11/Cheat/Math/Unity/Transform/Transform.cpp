#include "Transform.hpp"
#include "../../../Cheat.hpp"
#include "../../../Memory/Memory.hpp"

int ScreenWidth = 0;
int ScreenHeight = 0;

Vector3 W2S::World2Screen(Matrix4x4 ViewMatrix, Vector3 Pos) {
    Vector3 screen;

    float clipW = ViewMatrix._14 * Pos.X + ViewMatrix._24 * Pos.Y + ViewMatrix._34 * Pos.Z + ViewMatrix._44;
    if (clipW < 0.01f) {
        screen.Z = -1.0f;
        return screen;
    }

    float clipX = ViewMatrix._11 * Pos.X + ViewMatrix._21 * Pos.Y + ViewMatrix._31 * Pos.Z + ViewMatrix._41;
    float clipY = ViewMatrix._12 * Pos.X + ViewMatrix._22 * Pos.Y + ViewMatrix._32 * Pos.Z + ViewMatrix._42;

    screen.X = (ScreenWidth / 2.0f) + (ScreenWidth / 2.0f) * (clipX / clipW);
    screen.Y = (ScreenHeight / 2.0f) - (ScreenHeight / 2.0f) * (clipY / clipW);
    screen.Z = clipW;

    return screen;
}

Vector3 W2S::GetCameraWorldPos(uint32_t cameraNativePtr) {
    if (cameraNativePtr == 0) return Vector3::Zero();

    Matrix4x4 W2C = g_Memory->Read<Matrix4x4>(cameraNativePtr + 0x68);

    if (W2C._11 == 0.f && W2C._22 == 0.f && W2C._33 == 0.f)
        return Vector3::Zero();

    return Vector3(
        -(W2C._11 * W2C._41 + W2C._12 * W2C._42 + W2C._13 * W2C._43),
        -(W2C._21 * W2C._41 + W2C._22 * W2C._42 + W2C._23 * W2C._43),
        -(W2C._31 * W2C._41 + W2C._32 * W2C._42 + W2C._33 * W2C._43)
    );
}

Vector3 Transform::get_position_Injected(uint32_t Transform)
{
    // Ptrs core lidos com ReadPtr (retry x3 + validação de range) — cadeia
    // até 60 níveis de RPMs por player, cada falha silenciosa aqui causava
    // Vector3::Zero() → entity descartada no SharedFrame → pisca no cliente.
    auto transObj = g_Memory->ReadPtr(Transform + 0x8);
    if (!Memory::IsValidPtr(transObj)) return Vector3::Zero();

    auto matrix = g_Memory->ReadPtr(transObj + 0x20);
    auto index  = g_Memory->Read<uint32_t>(transObj + 0x24);
    if (!Memory::IsValidPtr(matrix)) return Vector3::Zero();

    auto matrix_list    = g_Memory->ReadPtr(matrix + Cheat::g_DrawOff.MATRIXLIST);
    auto matrix_indices = g_Memory->ReadPtr(matrix + Cheat::g_DrawOff.MATRIXIDX);
    if (!Memory::IsValidPtr(matrix_list) || !Memory::IsValidPtr(matrix_indices)) return Vector3::Zero();

    Vector3 result = g_Memory->Read<Vector3>(matrix_list + sizeof(TMatrix) * index);
    int transformIndex = g_Memory->Read<int>(matrix_indices + sizeof(int) * index);

    int curIndex = 0;
    while (transformIndex >= 0 && curIndex++ < 60)
    {
        TMatrix tMatrix = g_Memory->Read<TMatrix>(matrix_list + sizeof(TMatrix) * transformIndex);

        float rotX = tMatrix.Rotation.x;
        float rotY = tMatrix.Rotation.y;
        float rotZ = tMatrix.Rotation.z;
        float rotW = tMatrix.Rotation.w;

        float scaleX = result.X * tMatrix.Scale.x;
        float scaleY = result.Y * tMatrix.Scale.x;
        float scaleZ = result.Z * tMatrix.Scale.z;

        result.X = tMatrix.Position.x + scaleX + (scaleX * ((rotY * rotY * -2.0f) - (rotZ * rotZ * 2.0f))) + (scaleY * ((rotW * rotZ * -2.0f) - (rotY * rotX * -2.0f))) + (scaleZ * ((rotZ * rotX * 2.0f) - (rotW * rotY * -2.0f)));
        result.Y = tMatrix.Position.y + scaleY + (scaleX * ((rotX * rotY * 2.0f) - (rotW * rotZ * -2.0f))) + (scaleY * ((rotZ * rotZ * -2.0f) - (rotX * rotX * 2.0f))) + (scaleZ * ((rotW * rotX * -2.0f) - (rotZ * rotY * -2.0f)));
        result.Z = tMatrix.Position.z + scaleZ + (scaleX * ((rotW * rotY * -2.0f) - (rotX * rotZ * -2.0f))) + (scaleY * ((rotY * rotZ * 2.0f) - (rotW * rotX * -2.0f))) + (scaleZ * ((rotX * rotX * -2.0f) - (rotY * rotY * 2.0f)));

        transformIndex = g_Memory->Read<int>(matrix_indices + sizeof(int) * transformIndex);
    }

    return result;
}

Vector3 Transform::GetRootPosition(uint32_t Entity) {
    auto rootNode = g_Memory->ReadPtr(Entity + Cheat::g_DrawOff.ROOTNODE);
    if (!Memory::IsValidPtr(rootNode)) return Vector3::Zero();

    auto rootTF = g_Memory->ReadPtr(rootNode + Cheat::g_DrawOff.ITRANSFORM);
    if (!Memory::IsValidPtr(rootTF)) return Vector3::Zero();

    return get_position_Injected(rootTF);
}

// GetHeadPosition — usa HeadNode bone (Player + HEADNODE → ITRANSFORM →
// get_position_Injected).
//
// Mantida simples e consistente: lê o bone de animação da cabeça via a
// cadeia de transform do Unity (idêntico ao que o C# DMA WithoutCoffeBRDMA
// faz — HeadChain populado via Transform.PopulateChain do HeadNode).
//
// A abordagem via CapsuleCollider (m_fireColliders, Entity+0x760) foi
// tentada antes, mas o offset 0x38 (male) era a cápsula do PEITO na versão
// atual do jogo — causava o aimbot apontar ao peito no snap, depois subir
// lentamente à cabeça à medida que a chain CapsuleCollider falhava e
// caía pro HeadNode. Removido para consistência.
Vector3 Transform::GetHeadPosition(uint32_t Entity)
{
    auto HeadNode = g_Memory->ReadPtr(Entity + Cheat::g_DrawOff.HEADNODE);
    if (!Memory::IsValidPtr(HeadNode)) return Vector3::Zero();

    auto ITransformNode = g_Memory->ReadPtr(HeadNode + Cheat::g_DrawOff.ITRANSFORM);
    if (!Memory::IsValidPtr(ITransformNode)) return Vector3::Zero();

    return get_position_Injected(ITransformNode);
}

// GetChestPosition — mesma cadeia CapsuleCollider do GetHeadPosition,
// mudando só o offset do slot dentro do array de capsulas:
//   Male   → Chest = 0x14
//   Female → Chest = 0x18
// (offsets extraídos do bloco 0x10..0x54 documentado em Draw.cpp).
// Usado pelo Silent Aim quando o combo "Silent Target" está em "Chest".
Vector3 Transform::GetChestPosition(uint32_t Entity)
{
    // IsFemale nao tem backing field em Player (v7a 1.132.1) — usa slot masculino
    uint32_t posChest = 0x14u;

    uint32_t ListTransform = g_Memory->Read<uint32_t>(Entity + 0x7B4);  // atualizado 2026-09-16 (era 0x760) - List<CapsuleCollider> @ Player+0x7B4
    if (ListTransform != 0) {
        uint32_t TFC = g_Memory->Read<uint32_t>(ListTransform + 0x8);
        if (TFC != 0) {
            uint32_t Location = g_Memory->Read<uint32_t>(TFC + posChest);
            if (Location != 0) {
                uint32_t H1 = g_Memory->Read<uint32_t>(Location + 0x8);
                if (H1 != 0) {
                    uint32_t H2 = g_Memory->Read<uint32_t>(H1 + 0x28);
                    if (H2 != 0) {
                        uint32_t H3 = g_Memory->Read<uint32_t>(H2 + 0x14);
                        if (H3 != 0) {
                            return g_Memory->Read<Vector3>(H3 + 0x60);
                        }
                    }
                }
            }
        }
    }

    // Fallback: se a chain do CapsuleCollider falhar, usa a root position
    // (não temos um "ChestNode" mapeado como HEADNODE, então root é o mais
    // próximo semanticamente — evita retornar Zero e o Silent perder o alvo).
    return GetRootPosition(Entity);
}

Vector3 Transform::GetBonePosition(uint32_t Entity, uint32_t boneOffset)
{
    auto node = g_Memory->Read<uint32_t>(Entity + boneOffset);
    if (node == 0) return Vector3::Zero();

    auto tnode = g_Memory->Read<uint32_t>(node + Cheat::g_DrawOff.ITRANSFORM);
    if (tnode == 0) return Vector3::Zero();

    return get_position_Injected(tnode);
}