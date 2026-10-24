# RetroEngine + RetroEditor

Motor de juegos en primera persona con estética **PlayStation 1**, escrito desde cero en
**C++20 sobre Raylib 5.5**, con un **editor visual** (`RetroEditor`) para construir las escenas
sin editar JSON a mano.

El objetivo es poder hacer juegos pequeños y medianos de exploración, aventura u horror al estilo
PS1 (tipo *walking simulator* con puzles) y, a la larga, hacerlo todo desde el editor: cambiar
texturas, arrastrar modelos, editar las propiedades de cada objeto y la lógica del juego.

> La explicación detallada de cómo funciona cada sistema (render, colisiones, colisión
> automática, luces, pasos, audio, triggers, guardado, editor...) está en
> [docs/SISTEMA.md](docs/SISTEMA.md).

---

## Índice

1. [Estado actual](#estado-actual)
2. [Compilar y ejecutar](#compilar-y-ejecutar)
3. [Controles](#controles)
4. [Estructura del repositorio](#estructura-del-repositorio)
5. [Arquitectura en una página](#arquitectura-en-una-página)
6. [Decisiones de diseño y por qué](#decisiones-de-diseño-y-por-qué)
7. [Alcance (scope)](#alcance-scope)
8. [Problemas y deuda conocidos](#problemas-y-deuda-conocidos)
9. [Hoja de ruta](#hoja-de-ruta)
10. [Reglas de trabajo del proyecto](#reglas-de-trabajo-del-proyecto)

---

## Estado actual

El proyecto avanza por **hitos numerados**. El último entregado es el **Hito 26** (editor de
lógica), que sigue al 25 (editor de escenas). Lo que existe hoy:

| Área | Qué hay |
|---|---|
| Render PS1 | Resolución interna 320x240, temblor de vértices, texturas afines, color de 15 bits con *dithering* Bayer, niebla, iluminación por vértice, post-proceso (escalado nítido, viñeta, grano, tinte). |
| Mundo | Escenas en JSON, entidades con modelo `.obj`, plano procedural o sprite *billboard*, luz y sonido propios, rotación continua, personajes que miran al jugador y se balancean. |
| Jugador | Primera persona, cilindro que choca contra cajas orientadas, escalones, salto, gravedad, límites de escena. |
| Lógica | Triggers (zonas) con eventos `enter`/`exit`/`stay`, banderas, condiciones anidadas (`all`/`any`/`not`), interacción con **E**, visibilidad condicional (`showIf`), cambio de escena con fundido. |
| Audio | Música y ambiente con fundido cruzado, efectos (también posicionales), emisores en bucle con atenuación y paneo, pasos. |
| Guardado | Ranuras en `%APPDATA%/RetroEngine/saves`, escritura atómica con copia `.bak`, autoguardado. |
| Editor | Visor 3D con cámara libre, vista PS1 opcional, selección por clic, gizmo de mover, jerarquía, inspector, panel de assets con arrastrar y soltar, deshacer/rehacer, guardar/abrir/crear escenas. Editor de lógica: acciones, condiciones, interacciones y `showIf` en formularios, y un panel de validación que avisa de referencias rotas y banderas sospechosas. |
| Herramientas | `tools/texconv.py` (imagen a textura PS1) y `tools/charatlas.py` (foto de cara a atlas de personaje). |

Los assets actuales (modelos, texturas, audio) son **de relleno**, generados con scripts. No es
arte final.

Hay tres escenas de ejemplo conectadas entre sí: `demo.json` (puzle de llave y 3 monedas que abre
un portal), `noche.json` y `aldea.json`.

---

## Compilar y ejecutar

### Requisitos

- CMake 3.20 o superior.
- Un compilador C++20. El proyecto se desarrolla en **Windows con MinGW-w64** (MSYS2,
  `C:/msys64/mingw64`).
- Conexión a internet la primera vez: CMake descarga Raylib 5.5, nlohmann/json 3.11.3, Dear ImGui
  (rama `docking`) y rlImGui (rama `main`) con `FetchContent`.
- Para las herramientas: Python 3 con `pillow` y `numpy` (`tools/requirements.txt`).

### Compilar

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j8
```

Si no quieres compilar el editor, añade `-DRETRO_BUILD_EDITOR=OFF` al configurar.

Los ejecutables quedan en `build/bin/`:

- `RetroEngine.exe`: el juego. Lee los assets de `build/bin/assets/`, una **copia** que el
  destino `CopyAssets` sincroniza en cada compilación.
- `RetroEditor.exe`: el editor. Edita directamente los assets **de origen** (`assets/` del
  proyecto; la ruta se fija al compilar con `RETRO_PROJECT_ASSETS_DIR`). Se puede cambiar con
  `RetroEditor.exe --assets <carpeta>`.

> Importante: como el juego lee la copia, después de guardar una escena en el editor hay que
> **volver a compilar** (o al menos ejecutar el destino `CopyAssets`) para verla en el juego.

---

## Controles

### Juego

| Tecla | Acción |
|---|---|
| W A S D / ratón | Moverse / mirar |
| Mayús | Correr |
| Espacio | Saltar |
| E | Interactuar con lo que miras |
| F9 / F10 / Mayús+F10 | Guardar en la ranura 1 / cargar la ranura 1 / cargar el autoguardado |
| Tab / Mayús+Tab | Escena siguiente / anterior (de las ya conocidas) |
| F1 | Mostrar u ocultar la ayuda |
| F2 | Alternar entre primera persona y cámara orbital |
| F3 | Mostrar las cajas de colisión |
| F5 | Recargar la escena desde el disco |
| F6 | Reiniciar el estado (banderas y `once`) |
| F7 / F8 | Modo de escalado / activar o quitar el acabado del post-proceso |
| 1 2 3 4 5 6 | Temblor, color de 15 bits, *dithering*, niebla, nivel de textura afín, luces puntuales |
| M | Silenciar |
| F11 | Ventana sin bordes |
| Esc | Salir |

### Editor

| Entrada | Acción |
|---|---|
| Clic izquierdo | Seleccionar (otro clic en el mismo sitio elige el siguiente objeto en esa línea) |
| Arrastrar flechas del gizmo | Mover en X, Y o Z (Ctrl: ajustar a 0.25) |
| Arrastrar la esfera central | Mover sobre el plano horizontal |
| Botón derecho + ratón, W A S D, Q E | Mirar y volar (Mayús: más rápido; rueda: cambia la velocidad) |
| Rueda | Acercar o alejar |
| Botón central | Desplazar la vista |
| Arrastrar desde Assets | Modelo o sonido: se crea en el suelo. Textura sobre un objeto: se la cambia. Textura sobre el vacío: crea un sprite |
| Ctrl+S, Ctrl+Z, Ctrl+Y (o Ctrl+Mayús+Z), Ctrl+D, Supr, F, F5 | Guardar, deshacer, rehacer, duplicar, eliminar, enfocar, recargar |

---

## Estructura del repositorio

```
CMakeLists.txt        retro_core (biblioteca), RetroEngine (juego), RetroEditor (editor)
src/
  main.cpp            bucle del juego: jugador, triggers, interacción, guardado, depuración
  core/               RetroEngine (ventana, audio, assets, shaders, UI, post) y RenderTarget
  assets/             Resources (envoltorios RAII) y AssetManager (cachés con shared_ptr)
  graphics/           RetroShader, PostProcess, ShaderScope, Billboard, Light, MeshFactory,
                      Transform, RetroCamera, DebugDraw
  physics/            Collision: cajas orientadas, círculo contra caja, rayo contra caja
  player/             Player: controlador en primera persona
  world/              Entity, Scene, SceneManager, SceneLoader, Logic, Trigger, ActionRunner
  audio/              AudioSystem, Emitter, Footsteps
  game/               GameState (banderas y mensajes), SaveStorage, SaveGame
  ui/                 Ui: texto y rectángulos sobre la imagen final
editor/               RetroEditor: EditorApp, EditorLogic (acciones, condiciones, interacción),
                      EditorValidation (panel Validacion), EditorCamera, AssetCatalog,
                      JsonUtil, JsonFormat
assets/
  scenes/             demo.json, noche.json, aldea.json
  shaders/            retro.vs, retro.fs, retro_unlit.fs, retro_sprite.fs, post.fs
  models/ textures/ audio/ fonts/
tools/                texconv.py, charatlas.py
docs/SISTEMA.md       funcionamiento detallado de todos los sistemas
```

---

## Arquitectura en una página

```
                         +--------------------------------------+
  main.cpp (juego)  ---> |  RetroEngine                         |
  editor/main.cpp   ---> |   WindowGuard, AudioGuard            |
                         |   RenderTarget 320x240               |
                         |   AssetManager (cachés)              |
                         |   RetroShader (lit / unlit / sprite) |
                         |   Ui, PostProcess, AudioSystem       |
                         +--------------------------------------+
                                        |
          +-----------------------------+------------------------+
          |                                                      |
   SceneManager (juego)                                 EditorApp (editor)
     carga JSON -> Scene                                  JSON = fuente de verdad
     fundidos, entorno, audio                             Scene = vista previa
          |                                               reconstruida en cada cambio
        Scene: entidades, triggers, luces, emisores, límites, inicio
          |
   Player (colisión)  ActionRunner (acciones)  GameState (banderas)  SaveManager
```

Cada fotograma del juego: entrada y depuración, actualizar escena (animaciones, fundidos),
visibilidad (`showIf`), recoger colisiones, mover al jugador, pasos, audio posicional, triggers,
interacción, y después dibujar la escena en la textura de 320x240, escalarla a la ventana con el
post-proceso y dibujar la interfaz encima. El detalle está en
[docs/SISTEMA.md](docs/SISTEMA.md#2-el-fotograma-del-juego-paso-a-paso).

---

## Decisiones de diseño y por qué

### Tecnología

| Decisión | Por qué |
|---|---|
| **C++20 + Raylib 5.5** en vez de un motor comercial | Control total del pipeline de render (los efectos PS1 se hacen en shaders propios) y un motor pequeño que se entiende entero. Raylib da ventana, OpenGL 3.3, audio y carga de `.obj`/imágenes sin imponer arquitectura. |
| **Dependencias con `FetchContent`** | Clonar y compilar, sin instalar nada a mano. Raylib y nlohmann están fijados a versión. ImGui y rlImGui todavía no (deuda conocida). |
| **ImGui y rlImGui con `SOURCE_SUBDIR _sin_cmake`** | Descargarlos sin ejecutar su CMake y compilar solo los `.cpp` necesarios, con control total de las opciones. |
| **Una biblioteca `retro_core` compartida por juego y editor** | El editor usa *exactamente* el mismo cargador, la misma escena y el mismo render que el juego: lo que ves en el editor es lo que carga el juego. |

### Render

| Decisión | Por qué |
|---|---|
| **Renderizar a 320x240 y escalar** | Es la resolución típica de PS1; además abarata el render y da el píxel grueso de verdad, no un filtro que lo imite. |
| **Temblor de vértices en el *vertex shader*** (redondear la posición en pantalla a la rejilla de 320x240) | La PS1 no tenía precisión subpíxel; los polígonos "saltan" de píxel en píxel. Es barato y se puede desactivar (tecla 1). |
| **Textura afín mezclada (`affineAmount` = 0.35)** en vez de afín pura | La deformación afín completa es auténtica pero cansa la vista en suelos grandes. Mezclar ambas UV deja el efecto reconocible y ajustable (tecla 5: 0.35, 1, 0). |
| **Iluminación por vértice (Gouraud)** | Es lo que hacía la PS1, cuesta poco, y con modelos de pocos polígonos da ese degradado "a trozos" típico. |
| **Color de 15 bits con *dithering* Bayer 4x4** | La PS1 dibujaba en 15 bits con trama ordenada; los degradados se ven con bandas y trama, no suaves. |
| **Niebla con la profundidad `w` medida antes del temblor** | La niebla oculta el final del mundo (como en muchos juegos de PS1) y medirla antes del redondeo evita que tiemble con los vértices. El color de niebla es el del fondo, así lo lejano se funde con el cielo. |
| **Máximo 8 luces puntuales, las más cercanas** | Los *uniform arrays* del shader tienen tamaño fijo. Se eligen por `distancia - radio`, así una luz grande y lejana puede ganar a una pequeña y cercana. |
| **Sprites como *billboards* verticales con recorte por alfa** | Árboles, arbustos y farolas en 2D como en la época. Al recortar con `discard` (sin semitransparencia) no hay que ordenarlos por distancia. Son verticales para que no se inclinen al mirar desde arriba. |
| **Escalado "bilineal nítido" por defecto** | El escalado por vecino más cercano a tamaños no enteros deja píxeles de distinto ancho y brillos al moverse; el bilineal emborrona. El nítido mantiene píxeles cuadrados y solo suaviza el borde entre ellos. |
| **Texturas con filtro de punto** | Texeles visibles, como en la consola. |

### Mundo y lógica

| Decisión | Por qué |
|---|---|
| **Escenas en JSON** | Separar datos y código: se edita el juego sin recompilar (F5 recarga) y el editor solo tiene que escribir un archivo. |
| **Carga transaccional** | La escena nueva se construye aparte; si el JSON tiene un error, la anterior sigue intacta y se restauran cámara, niebla, luz y fondo. Un error de datos nunca deja el juego roto a medias. |
| **Errores con contexto** (`scenes/demo.json (entidad #3 'casa'): ...`) | Muchos fallos fueron rutas mal escritas; el mensaje dice exactamente dónde mirar. |
| **Una entidad es modelo *o* sprite**, con componentes opcionales (`collider`, `light`, `sound`, `interact`...) | Modelo simple, sin un sistema ECS: suficiente para el tamaño de juego buscado y fácil de leer. |
| **Lógica como datos** (acciones `std::variant`, árbol de condiciones) | Los puzles se escriben en el JSON y el editor podrá editarlos con formularios. `ActionRunner` es el único que conoce a la vez escenas, jugador, estado y audio. |
| **Banderas enteras en `GameState`** | Un único mecanismo para todo: llaves (0/1), contadores (monedas), `once` de triggers e interacciones. Fácil de guardar y de usar en textos (`{monedas}`). |
| **Los `once` se guardan como banderas con la ruta de la escena** | Sobreviven al cambio de escena y a guardar/cargar sin código extra. |
| **Una condición que falla no gasta el `once`** | Así un trigger "abrir puerta" puede fallar mientras no tengas la llave y funcionar después. |
| **El primer fotograma tras cargar no dispara triggers** | Si apareces dentro de una zona `enter` no se dispara de golpe; solo cuenta entrar de verdad. |
| **Nombres únicos de entidades y triggers** | Las acciones y condiciones se refieren a ellos por nombre; el cargador rechaza duplicados. |

### Física

| Decisión | Por qué |
|---|---|
| **Sin motor de física: cilindro contra cajas orientadas** | Un juego de exploración solo necesita no atravesar paredes, subir escalones y pisar plataformas. Es predecible, rápido y depurable (F3). |
| **Cajas giradas solo en Y** | Casi todo en estos escenarios está de pie; girar en Y cubre casas, muros y muebles con una prueba círculo-rectángulo en 2D más un rango de altura. |
| **La caja usa la misma matriz que el dibujo** | La colisión siempre coincide con el modelo, también si se escala o gira. |
| **Subpasos de 1/120 s** | A velocidad de carrera, en un fotograma lento, el jugador podría atravesar una pared fina. |
| **Colisión automática (`autoCollide`)** | Poner un `.obj` en la escena y que ya sea sólido, sin escribir cajas a mano. Los planos no la usan: el suelo es el plano Y=0. |

### Audio

| Decisión | Por qué |
|---|---|
| **Música y ambiente como *streams* con fundido cruzado** | Cambiar de escena sin cortes bruscos; si la pista es la misma, sigue sonando. |
| **Emisores en bucle como *streams* con inicio aleatorio** | Dos emisores con el mismo archivo no suenan en fase. |
| **Como mucho 6 voces de emisor, las más audibles** | Limita el coste y el ruido; las voces que dejan de oírse se apagan con suavidad y se liberan. |
| **Si no hay dispositivo de audio, el juego sigue mudo** | Un PC sin salida de audio no debe impedir jugar ni probar. |

### Guardado

| Decisión | Por qué |
|---|---|
| **Escritura atómica (`.tmp` y luego renombrar) con copia `.bak`** | Un corte de luz o un disco lleno a mitad de guardar nunca destruye la partida anterior; si el archivo está dañado se carga la copia. |
| **`SaveStorage` solo con la biblioteca estándar** | Se puede probar aislado, sin ventana ni Raylib. El editor lo reutiliza para guardar escenas. |
| **Carpeta de datos del usuario (`%APPDATA%`)** | La carpeta del ejecutable puede no tener permisos de escritura. |
| **Formato con `version`** | Detectar guardados de una versión más nueva y rechazarlos con un mensaje claro. |

### Editor

| Decisión | Por qué |
|---|---|
| **El JSON es la fuente de verdad; la escena es una vista previa** | No hay dos modelos de datos que sincronizar. Lo que no sabe editar (lógica, claves desconocidas) se conserva intacto al guardar. |
| **Reconstruir la escena entera en cada cambio** (una vez por fotograma) | Simple y robusto: usa el cargador real, así el editor nunca acepta algo que el juego rechazaría. Si el cambio es inválido, se revierte y se explica en Mensajes. Con escenas pequeñas el coste es aceptable. |
| **`nlohmann::ordered_json`** | Mantiene el orden de las claves: al guardar, el archivo cambia lo mínimo (diffs legibles en git). |
| **Formateador propio con respaldo** | `dump(2)` pone cada número de un vector en su línea. El formateador deja lo corto en una línea; si su salida no reproduce el mismo JSON, se usa `dump(2)`. |
| **Deshacer por instantáneas del documento** | Trivialmente correcto para cualquier cambio. Los cambios continuos (arrastrar) se funden en una sola entrada. |
| **Operaciones estructurales diferidas al siguiente fotograma** | Insertar o borrar en un arreglo invalida los punteros que los widgets están usando ese fotograma. |
| **El editor muestra todo visible** | Para poder colocar objetos que en el juego dependen de banderas (`showIf`). |
| **La lógica se edita con el mismo protocolo que el resto** | Acciones y condiciones son JSON como cualquier otro campo: si un cambio deja la escena inválida (por ejemplo, un grupo de condiciones vacío), el cargador real lo rechaza y se revierte. No hace falta duplicar las reglas del formato en el editor. |
| **Validación en un panel aparte, sin bloquear** | Lo que el cargador acepta pero fallará al jugar (una escena de destino que no existe, una bandera que nadie cambia) se avisa, no se impide: a mitad de construir una escena es normal tener cosas a medias. Las banderas se comparan con **todas** las escenas, porque una llave se puede recoger en otra. |
| **El editor edita `assets/` de origen** | Lo guardado va al repositorio, no a la copia de `build/`, que se sobrescribe al compilar. |

### Código

| Decisión | Por qué |
|---|---|
| **RAII estricto, sin `new`/`delete`** | Cada recurso de Raylib (textura, modelo, shader, sonido, render target, ventana, audio) está envuelto en una clase que lo libera. El orden de los miembros de `RetroEngine` garantiza que, por ejemplo, las texturas se liberan antes de cerrar la ventana. |
| **Cachés con `shared_ptr` y `PurgeUnused`** | Varias entidades comparten modelo y textura; al cambiar de escena se libera lo que ya nadie usa. |
| **Sin compatibilidad hacia atrás** | El motor es joven: si un formato cambia, se actualizan los JSON y el código, sin dejar caminos antiguos. Por ejemplo, el antiguo bloque `portals` se eliminó en favor de triggers con `goto_scene`. |

---

## Alcance (scope)

### Lo que el proyecto **es** hoy

Un motor para un juego de exploración en primera persona con aspecto PS1: varias escenas
conectadas por triggers, puzles simples con banderas (llaves, monedas, puertas), mensajes en
pantalla, interacción con **E**, personajes que te miran, luces de color con parpadeo, audio
posicional, partidas guardadas, y un editor para construir y modificar las escenas (colocar,
mover, texturizar, configurar) y su lógica (triggers, condiciones, interacciones) sin tocar el
JSON.

### Lo que el editor **todavía no** edita

Se muestra en solo lectura y se conserva intacto al guardar:

- `facePlayer`, `idle` y el `collider` personalizado (`{min, max}`).
- El bloque `audio` de la escena (música, ambiente, pasos) y el parpadeo (`flicker`,
  `flickerRate`) de las luces.
- Cualquier clave desconocida.

Además el editor no tiene selección múltiple, copiar y pegar entre escenas, gizmos de rotación o
escala, reordenar la jerarquía, vista previa de audio ni "jugar en el editor".

### Fuera de alcance (el "techo" del motor)

No pretende ser un motor para multijugador, física compleja, mundos abiertos con *streaming*,
animación esquelética ni render moderno (PBR, sombras dinámicas). La estética PS1 es un objetivo,
no una limitación que haya que esconder.

También faltan, aunque sí entran en el alcance futuro: menú principal y pausa, NPCs que caminen,
sombras simples (*blob shadows*), más primitivas que `plane`, ejecutable empaquetado,
localización y mando.

---

## Problemas y deuda conocidos

Encontrados al revisar el código (no todos verificados en ejecución):

- **`wrap` se aplica a la textura compartida.** Si dos entidades usan la misma textura con
  distinto `wrap`, gana la última que se cargó.
- **Las cajas de colisión ignoran el giro en X y Z.** Un objeto inclinado sigue teniendo una caja
  vertical.
- **El desplazamiento de `light` y `sound` de una entidad se escala pero no gira** con ella.
- **Los pasos son por escena, no por superficie**: cada escena define una lista de sonidos de
  paso.
- **Referencias por nombre frágiles**: renombrar o borrar una entidad no actualiza las acciones
  que la nombran. El editor avisa al borrar y el panel Validacion marca la referencia rota, pero
  no la arregla.
- **Dependencias sin fijar**: ImGui (`docking`) y rlImGui (`main`) siguen ramas.
- El título de la ventana del juego todavía dice "Hito 16", y `main.cpp` solo conoce
  `scenes/demo.json` al empezar (las demás se añaden al visitarlas).

---

## Hoja de ruta

Orden sugerido:

1. ~~Editor de lógica y panel de validación~~ (Hito 26, hecho).
2. Copiar y pegar, selección múltiple, gizmos de rotación y escala, ajuste configurable.
3. "Jugar" dentro del editor y recarga en caliente.
4. Menú principal y pausa, NPCs con rutas simples, *blob shadows*, más tipos de acción.
5. Empaquetado del juego (ejecutable + assets) y fijar las dependencias.

Pendientes pequeños del editor: `facePlayer`, `idle`, la caja de colisión personalizada, el
bloque `audio` de la escena y el parpadeo de las luces.

---

## Reglas de trabajo del proyecto

- Código, comentarios y documentación en español. En la interfaz (ImGui y mensajes del juego),
  **ASCII sin tildes**, para no depender de los glifos de la fuente.
- RAII estricto, sin `new`/`delete`. El orden de los miembros es el inverso del orden de
  destrucción.
- Sin código de compatibilidad hacia atrás: si cambia un formato o una API, se cambia todo y se
  actualizan los JSON.
- Ante un comportamiento raro, mirar primero los logs de la consola: muchos fallos han sido
  rutas mal escritas.
- Cada hito se entrega con los archivos cambiados, una explicación breve y qué se debería ver al
  probarlo.
