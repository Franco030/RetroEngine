#pragma once

#include "AssetCatalog.hpp"
#include "EditorCamera.hpp"
#include "JsonUtil.hpp"

#include "core/RenderTarget.hpp"
#include "core/RetroEngine.hpp"
#include "graphics/Light.hpp"
#include "world/Scene.hpp"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace retro {
class RetroCamera;
}

namespace editor {

enum class SelectionKind { None, Entity, Trigger, Light, Emitter };

struct Selection {
  SelectionKind kind = SelectionKind::None;
  int index = -1;
};

// Un problema que encuentra el panel Validacion. 'kind' e 'index' dicen que seleccionar al
// hacer clic (None si no hay un sitio concreto).
enum class Severity { Error, Warning, Info };
struct Issue {
  Severity severity = Severity::Warning;
  std::string text;
  SelectionKind kind = SelectionKind::None;
  int index = -1;
};

class EditorApp {
public:
  // initialScene: ruta relativa a assets (p. ej. "scenes/aldea.json"); vacia = demo.json o la
  // primera que haya.
  EditorApp(retro::RetroEngine& engine, const std::string& initialScene);
  ~EditorApp();

  EditorApp(const EditorApp&) = delete;
  EditorApp& operator=(const EditorApp&) = delete;

  void Run();

private:
  struct LogLine {
    bool error = false;
    std::string text;
  };
  struct EnvDefaults {
    Color background{};
    float fogStart = 0.0f;
    float fogEnd = 0.0f;
    Vector3 lightDirection{};
    float ambient = 0.0f;
    float sun = 1.0f;
    float mistDensity = 0.0f;
    float mistHeight = 1.5f;
    Vector3 mistColor{};
  };
  struct Hit {
    SelectionKind kind;
    int index;
    float distance;
    int rank;
  };
  enum class PendingKind { None, OpenScene, Quit, NewScene };
  struct Pending {
    PendingKind kind = PendingKind::None;
    std::string path;
  };
  enum class Deferred { None, Delete, Duplicate, AddFlame };

  // ------------------------------------------------------------------ escena y archivo
  void RefreshSceneList();
  bool OpenScene(const std::string& relativePath, bool frameCamera);
  bool Rebuild(const Json& candidate, const std::string& path, std::string* error,
               retro::RetroCamera* camera = nullptr);
  void ResetEnvironment();
  void ApplyViewMode();
  bool Save();
  bool CreateScene(const std::string& rawName);
  void UpdateDirty();
  void UpdateTitle();
  void Log(bool error, std::string text);

  // ------------------------------------------------------------------ cambios y deshacer
  void BeginChange(ImGuiID id);
  void MarkChanged(ImGuiID id);
  void FlushChanges();
  bool ApplyDocument(const Json& doc);
  void Undo();
  void Redo();
  void ValidateSelection();
  const Json* SelectedJson() const;
  Json* SelectedJsonMut();

  // ------------------------------------------------------------------ operaciones
  std::string UniqueName(const char* arrayKey, const std::string& base) const;
  void InsertItem(SelectionKind kind, Json item);
  void AddEntityModel(const std::string& model, Vector3 position);
  void AddEntitySprite(const std::string& texture, Vector3 position);
  void AddTrigger(Vector3 position);
  void AddLight(Vector3 position);
  void AddEmitter(const std::string& file, Vector3 position);
  void DuplicateSelection();
  void DeleteSelection();
  void FocusSelection();
  void ApplyTextureToEntity(int index, const std::string& texture);
  void HandleAssetDrop(const std::string& path, Vector2 mouse);
  Vector3 PlacementPoint() const;
  // Caja de la entidad 'index' en el mundo (alineada a los ejes: abarca el modelo girado).
  bool EntityWorldBox(int index, BoundingBox& out) const;
  // Pone una llama animada (sprite) en lo alto del modelo 'index' y, si no tiene, una luz.
  void AddFlameOn(int index);
  void AddAnimatedFlame(Vector3 position);
  void AddCandleGroup(Vector3 position);
  static Vector3 GroundPoint(const Ray& ray);
  bool SelectionCenter(Vector3& center, float& radius) const;

  void RequestOpen(const std::string& path);
  void RequestQuit();
  void RequestNewScene();
  void ExecutePending();
  void ExecuteDeferred();

  // ------------------------------------------------------------------ interfaz
  void DrawUi();
  void DrawMenuBar();
  void DrawStatusBar();
  void DrawAddItems();
  void BuildDefaultLayout(ImGuiID dock);
  void DrawHierarchy();
  void DrawInspector();
  void InspectEntity(Json& e);
  void InspectTrigger(Json& t);
  void InspectLight(Json& l);
  void InspectEmitter(Json& e);
  void InspectScene();
  // "audio" de la escena: musica y ambiente ({ "file", "volume" }) y pasos ({ "files", "volume" }).
  void AudioTrack(Json& d, const char* key, const char* title, float defVolume);
  void AudioFootsteps(Json& d);
  void DrawAssets();
  void AssetList(const std::vector<std::string>& files, bool thumbnails);
  void Thumbnail(const std::string& relative, float maxSize);
  std::shared_ptr<retro::TextureResource> ThumbTexture(const std::string& relative);
  void DrawViewport();
  void DrawViewportToolbar();
  void DrawMessages();
  void DrawValidation(); // EditorValidation.cpp
  void RunValidation();
  void DrawModals();
  void HandleShortcuts();

  // ------------------------------------------------------------------ campos editables
  // 'parent' nulo = campo de la raiz; si no, de un objeto hijo. Si eraseDefault, un valor
  // igual al de por defecto se quita del JSON en vez de escribirse.
  bool FieldVec3(const char* label, Json& root, const char* parent, const char* key, Vector3 def,
                 float speed, bool eraseDefault = true);
  bool FieldVec2(const char* label, Json& root, const char* parent, const char* key, Vector2 def,
                 float speed, bool eraseDefault = true);
  bool FieldFloat(const char* label, Json& root, const char* parent, const char* key, float def,
                  float speed, float minValue, float maxValue, bool eraseDefault = true);
  bool FieldInt(const char* label, Json& root, const char* parent, const char* key, int def,
                int minValue, int maxValue, bool eraseDefault = true);
  bool FieldBool(const char* label, Json& root, const char* parent, const char* key, bool def,
                 bool eraseDefault = true);
  bool FieldColor(const char* label, Json& root, const char* parent, const char* key, Color def,
                  bool alpha);
  bool FieldFile(const char* label, Json& root, const char* parent, const char* key,
                 const std::vector<std::string>& files, bool allowNone, bool thumbnail);
  bool FieldText(const char* label, Json& root, const char* parent, const char* key,
                 bool multiline = false);
  // Caja {"min": [...], "max": [...]} editada como centro y tamano (lo que se piensa al
  // ajustar una caja). El tamano nunca baja de 5 cm, asi min < max siempre.
  bool FieldBox(Json& box);
  // Caja local de la entidad 'index' de la escena: su colision si la tiene, si no la del
  // modelo o el sprite.
  BoundingBox EntityBox(int index, bool preferCollider) const;
  bool FieldChoice(const char* label, Json& root, const char* parent, const char* key,
                   const std::vector<std::string>& options, const std::string& def);

  // ------------------------------------------------------------------ logica (EditorLogic.cpp)
  // Lo que un elemento de una lista pide hacer con el (se aplica al terminar de dibujar la lista).
  enum class ListOp { None, Remove, Up, Down };
  // Posicion de un elemento dentro de su lista, para dibujar sus botones Subir/Bajar/Quitar.
  struct ListSlot {
    std::size_t index = 0;
    std::size_t count = 0;
    bool canRemove = true;
  };
  static ListOp ListButtons(const ListSlot& slot);
  void ApplyListOp(Json& list, ListOp op, std::size_t index);

  // Lista de acciones owner[key] (por ejemplo el "do" de un trigger): editar, anadir, quitar
  // y reordenar.
  // keepOne: la lista no puede quedarse vacia (el "do" de una interaccion lo exige).
  void EditActionList(Json& owner, const char* key, bool keepOne = false);
  void EditAction(Json& action);
  std::vector<std::string> EntityNames() const;

  // Condiciones owner[key] ("if" o "showIf"): ausente, un objeto o un arreglo (deben cumplirse
  // todas). Cada condicion es un arbol de "flag", "all", "any" y "not".
  void EditConditions(Json& owner, const char* key);
  void EditConditionList(Json& list, int depth, bool keepOne);
  // 'slot' nulo = la condicion no esta en una lista (la raiz unica o el hijo de un "not").
  ListOp EditCondition(Json& condition, int depth, const ListSlot* slot);

  // "interact" de la entidad 'entityIndex' (texto, alcance, una vez, caja, condiciones y acciones).
  void EditInteraction(Json& entity, int entityIndex);
  // "seen" de una entidad: acciones al verla (en el centro de la vista) y al dejar de mirarla.
  void EditSight(Json& entity);

  // ------------------------------------------------------------------ dialogos (EditorLogic.cpp)
  // Cada dialogo es un archivo aparte (assets/dialogues/<id>.json) que se edita en su panel y se
  // guarda solo. Mientras se dibuja ese panel (dialogueMode_), BeginChange/MarkChanged marcan el
  // dialogo y no la escena; los dialogos no tienen deshacer.
  void RefreshDialogueList();
  void OpenDialogue(const std::string& id);
  bool CreateDialogue(const std::string& id, Json content);
  void SaveDialogueIfChanged();
  void CheckDialogue(); // lo carga como el juego, para avisar de errores
  void DrawDialogues();
  void EditDialogueEntry(Json& entry, std::size_t index);
  // El editor de texto de una pagina (un string dentro de "lines").
  bool EditLine(const char* label, Json& value);
  // Mueve los mensajes del "do" de la interaccion a un dialogo nuevo con el nombre de la entidad.
  void MessagesToDialogue(Json& entity);
  std::string DialoguePath(const std::string& id) const;

  // ------------------------------------------------------------------ visor
  void EnsureTargets(int w, int h);
  void RenderViewport(int w, int h);
  void DrawWorld(const Camera3D& camera);
  void DrawGizmos();
  void DrawSelectionGizmo();
  // 'mouse' va relativo a la esquina de la imagen del visor.
  bool MouseRay(Vector2 mouse, Ray& out) const;
  std::vector<Hit> CollectHits(const Ray& ray) const;
  void PickAt(Vector2 mouse);
  void HandleViewportMouse(Vector2 mouse);

  // gizmo de mover: 0, 1, 2 = ejes X, Y, Z; 3 = esfera central (sobre el suelo); -1 = nada
  bool GizmoPosition(Vector3& out) const;
  float GizmoLength(Vector3 at) const;
  int GizmoHitTest(const Ray& ray) const;
  void BeginGizmoDrag(int handle, const Ray& ray);
  void UpdateGizmoDrag(const Ray& ray);

  retro::RetroEngine& engine_;
  EditorCamera camera_;
  EnvDefaults defaults_{};
  AssetCatalog assets_;

  std::string iniPath_;
  bool layoutPending_ = false;
  bool quit_ = false;

  // El JSON es la fuente de verdad del editor; 'scene_' es solo la vista previa de ejecucion.
  std::string scenePath_;
  Json doc_;
  Json savedDoc_;
  Json lastGood_;
  bool dirty_ = false;
  // Sube cada vez que doc_ cambia de verdad; la validacion se repite solo cuando cambia.
  std::uint64_t docRevision_ = 0;
  std::uint64_t validatedRevision_ = UINT64_MAX;
  std::vector<Issue> issues_;
  std::unique_ptr<retro::Scene> scene_;
  std::vector<std::string> sceneList_;
  Selection selection_;
  std::vector<LogLine> log_;
  bool logScroll_ = false;
  std::string windowTitle_;

  // deshacer y rehacer: copias del documento completo (las escenas son pequenas)
  std::vector<Json> undo_;
  std::vector<Json> redo_;
  std::optional<Json> gestureBase_;
  bool needRebuild_ = false;
  ImGuiID gestureId_ = 0;
  ImGuiID lastUndoId_ = 0;
  double lastUndoTime_ = -10.0;
  bool gestureEnded_ = false;

  Deferred deferred_ = Deferred::None;
  int deferredIndex_ = -1; // entidad sobre la que actua el diferido (AddFlame)
  Pending pending_;
  bool openUnsavedPopup_ = false;
  bool openNewScenePopup_ = false;
  char newSceneName_[64] = "";

  // texto del buscador de la jerarquia
  char filter_[64] = "";

  // campo de texto en edicion (se confirma al terminar de escribir)
  std::string textId_;
  char textBuf_[512] = "";

  // miniaturas y archivos que no se pudieron cargar
  std::unordered_map<std::string, std::shared_ptr<retro::TextureResource>> thumbs_;
  std::unordered_set<std::string> badAssets_;

  std::unique_ptr<retro::RenderTarget> viewTarget_;
  std::unique_ptr<retro::RenderTarget> sceneTarget_;
  std::vector<retro::PointLight> lights_;
  Rectangle ps1Rect_{};
  int viewW_ = 16;
  int viewH_ = 16;
  bool viewportHovered_ = false;
  bool lookingNow_ = false;

  Vector2 lastPick_{-1000.0f, -1000.0f};
  int gizmoHover_ = -1;
  int gizmoDrag_ = -1;
  Vector3 dragOrigin_{};
  Vector3 dragGrab_{};
  float dragParam_ = 0.0f;

  bool ps1View_ = false;
  bool fogOn_ = true;
  bool animate_ = false;
  bool cutCeiling_ = false;   // cortar lo que este por encima de cutHeight_ (ver interiores)
  float cutHeight_ = 3.0f;
  bool showGrid_ = true;
  bool showColliders_ = false;
  bool showTriggers_ = true;
  bool showLights_ = true;
  bool showEmitters_ = true;
  bool showBounds_ = true;
  bool showSpawn_ = true;

  // dialogos
  std::vector<std::string> dialogueList_; // ids: la ruta dentro de dialogues/, sin ".json"
  std::string dialogueId_;                // el abierto en el panel (vacio = ninguno)
  Json dialogueDoc_;
  std::string pendingDialogue_;           // abrir al empezar el panel (no a mitad de dibujarlo)
  std::string dialogueError_;
  bool dialogueMode_ = false;
  bool dialogueChanged_ = false;
  bool focusDialogues_ = false;
  char newDialogueName_[64] = "";
};

} // namespace editor
