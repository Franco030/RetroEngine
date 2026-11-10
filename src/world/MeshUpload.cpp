#include "world/MeshUpload.hpp"

#include "assets/AssetManager.hpp"
#include "core/RetroEngine.hpp"

#include <rlgl.h>

#include <utility>
#include <vector>

namespace retro {

std::shared_ptr<ModelResource> UploadMesh(RetroEngine& engine, TextureCache& cache, const ObjData& data) {
  std::vector<std::shared_ptr<TextureResource>> textures;
  for (const ObjData::Part& p : data.parts) {
    auto& t = cache[p.texture];
    if (!t) {
      t = engine.Assets().GetTexture(p.texture);
      SetTextureWrap(t->Get(), TEXTURE_WRAP_REPEAT);
      const Texture2D& tx = t->Get();
      int mips = 1;
      rlGenTextureMipmaps(tx.id, tx.width, tx.height, tx.format, &mips);
      rlTextureParameters(tx.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST_MIP_LINEAR);
    }
    textures.push_back(t);
  }
  auto model = std::make_shared<ModelResource>(data, std::move(textures));
  model->SetShader(engine.Retro().Lit());
  return model;
}

} // namespace retro
