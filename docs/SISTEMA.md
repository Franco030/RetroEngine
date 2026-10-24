# Cómo funciona el sistema

Este documento explica en detalle cómo funciona por dentro **RetroEngine** y **RetroEditor**:
qué hace cada sistema, en qué orden, con qué fórmulas y valores por defecto, y qué limitaciones
tiene. Está escrito a partir del código fuente; cuando algo es una limitación o un fallo
conocido se indica.

Para la visión general, las decisiones de diseño y el alcance, ver el [README](../README.md).

---

## Índice

0. [Convenciones](#0-convenciones)
1. [Arranque y propiedad de los recursos](#1-arranque-y-propiedad-de-los-recursos)
2. [El fotograma del juego, paso a paso](#2-el-fotograma-del-juego-paso-a-paso)
3. [Render: la estética PS1](#3-render-la-estética-ps1)
4. [Luces](#4-luces)
5. [Entidades](#5-entidades)
6. [Colisiones y colisión automática](#6-colisiones-y-colisión-automática)
7. [El jugador](#7-el-jugador)
8. [Triggers](#8-triggers)
9. [Condiciones](#9-condiciones)
10. [Acciones](#10-acciones)
11. [Interacción (tecla E)](#11-interacción-tecla-e)
12. [Audio](#12-audio)
13. [Pasos](#13-pasos)
14. [Escenas y cambio de escena](#14-escenas-y-cambio-de-escena)
15. [Estado del juego y partidas guardadas](#15-estado-del-juego-y-partidas-guardadas)
16. [Assets y caché](#16-assets-y-caché)
17. [Interfaz en pantalla](#17-interfaz-en-pantalla)
18. [Referencia completa del formato de escena](#18-referencia-completa-del-formato-de-escena)
19. [El editor](#19-el-editor)
20. [Herramientas de contenido](#20-herramientas-de-contenido)
21. [Depuración](#21-depuración)

---

## 0. Convenciones

- **Ejes**: Y hacia arriba. El suelo base es el plano **Y = 0**. Las coordenadas del plano
  horizontal se escriben `[x, z]` (por ejemplo en `bounds`).
- **Unidades**: aproximadamente metros. El jugador mide 1.7 y sus ojos están a 1.55.
- **Ángulos**: siempre en **grados** en el JSON. La rotación de una entidad es Euler XYZ.
- **Yaw** (giro horizontal del jugador, de la cámara y de los triggers): con yaw 0 se mira hacia
  **-Z**. La dirección hacia delante es `(-sin(yaw), 0, -cos(yaw))`. Mover el ratón a la
  derecha *resta* yaw.
- **Orientación de los modelos**: `facePlayer` gira la entidad para que su **+Z local** apunte al
  jugador, así que los modelos de personaje deben mirar hacia +Z en su archivo.
- **Rutas**: todas relativas a la carpeta de assets (`textures/suelo.png`, `scenes/demo.json`).
- **Colores**: `[r, g, b]` o `[r, g, b, a]` con valores de 0 a 255.

---

## 1. Arranque y propiedad de los recursos

`RetroEngine` (`src/core/RetroEngine.hpp`) es el dueño de todo lo que vive mientras dura el
programa. Sus miembros se construyen en este orden y se destruyen en el inverso:

| # | Miembro | Qué hace |
|---|---|---|
| 1 | `WindowGuard` | Abre la ventana y el contexto OpenGL 3.3 (`InitWindow`). Desactiva la tecla de salida de Raylib (Esc la gestiona el motor). La cierra al final. |
| 2 | `AudioGuard` | Abre el dispositivo de audio. Si no hay, avisa por consola y el juego sigue **sin sonido**. |
| 3 | `RenderTarget` | La textura interna de **320x240** donde se dibuja el mundo. |
| 4 | `AssetManager` | Cachés de texturas, modelos, shaders, fuentes y sonidos. |
| 5 | `RetroShader` | Los tres programas de shader del mundo (ver §3). |
| 6 | `Ui` | Texto y rectángulos sobre la imagen final. |
| 7 | `PostProcess` | El shader que escala la imagen interna a la ventana. |
| 8 | `AudioSystem` | Música, ambiente, efectos, emisores y pasos. |

Gracias a ese orden, por ejemplo, todas las texturas y shaders se liberan **antes** de cerrar la
ventana (liberarlos sin contexto OpenGL fallaría).

**Carpeta de assets**: `EngineConfig::assetRoot`. Si está vacía (el juego), se usa
`<carpeta del ejecutable>/assets/`. El editor la fija a la carpeta `assets/` del proyecto.

**Shaders**: se cargan al construir el motor: `shaders/retro.vs` con `retro.fs`,
`retro_unlit.fs` y `retro_sprite.fs`, y `shaders/post.fs` (con el vertex shader por defecto de
Raylib). `ShaderResource` comprueba primero que existan los archivos (Raylib, con una ruta
inexistente, usaría su shader por defecto sin dar error) y lanza `Shader inexistente: <ruta>` si
falta alguno; si el shader no compila, lanza `No se pudo cargar el shader`. En los dos casos el
programa termina con el mensaje en la consola.

**Configuración por defecto** (`EngineConfig`): ventana de 1280x960 redimensionable, 60 FPS,
vsync, resolución interna 320x240, color de fondo `(24, 20, 32)`, fuente `fonts/ui.ttf`. La
resolución interna y la de ventana también se pueden cambiar con las macros
`RETRO_INTERNAL_WIDTH/HEIGHT` y `RETRO_WINDOW_WIDTH/HEIGHT` del CMake.

**Bucle del motor** (`RetroEngine::Run`): en cada fotograma, Esc sale, F11 alterna la ventana
sin bordes, se actualiza el audio, se llama a la función `update` del juego, se dibuja el mundo
en la textura interna con la función `draw`, se presenta escalada con el post-proceso, y por
último se dibuja la interfaz (`overlay`) encima.

---

## 2. El fotograma del juego, paso a paso

`src/main.cpp` monta el juego: un `RetroEngine`, una `RetroCamera`, un `SceneManager` con
`scenes/demo.json`, un `Player`, un `GameState`, un `ActionRunner`, un `FootstepTracker` y un
`SaveManager`.

**Actualización**, en este orden:

1. Teclas de guardado, carga, cambio de escena, recarga, reinicio de estado y depuración
   (F1–F10, Tab).
2. `SceneManager::Update`: avanza el fundido (si hay cambio de escena) y actualiza las entidades
   (giro `spin`, `facePlayer`, `idle`). El "foco" para `facePlayer` son los pies del jugador.
3. `GameState::Update`: descuenta el tiempo del mensaje en pantalla.
4. Si se acaba de cargar una escena nueva (cambia `Generation()`): se coloca al jugador en el
   punto de inicio y, si venía de cargar partida, se restauran banderas, inclinación y tiempo de
   juego; si no, se **autoguarda**.
5. `RefreshVisibility`: recalcula `visible` de las entidades con `showIf`.
6. Se recogen las cajas de colisión del mundo (`CollectColliders`).
7. Se mueve al jugador contra esas cajas (solo en primera persona y fuera de un fundido) y se
   coloca la cámara en sus ojos. En modo orbital (F2), se mueve la cámara orbital.
8. Pasos (`FootstepTracker`).
9. Audio posicional: el oyente es la cámara; se recogen y actualizan los emisores.
10. Triggers: se evalúan contra el cilindro del jugador y se ejecutan las acciones disparadas.
11. Interacción: se busca la entidad interactuable que se mira; con **E** se ejecutan sus
    acciones.
12. `RefreshVisibility` otra vez, para que lo que cambiaron las acciones se vea en este mismo
    fotograma.

**Dibujo**, en la textura interna de 320x240:

1. Se eligen las luces puntuales y se dibuja la escena (modelos, luego sprites).
2. Con el shader sin luz se dibujan los marcos de los triggers con `marker` y, con F3, las cajas
   de colisión.
3. El rectángulo negro del fundido.

Después el motor escala la textura a la ventana con el post-proceso y dibuja la interfaz: la
mira (amarilla si hay algo con lo que interactuar), el texto "E: ...", la ayuda, el nombre de la
escena y el cuadro de mensajes.

---

## 3. Render: la estética PS1

### 3.1 Programas de shader

Los tres comparten el mismo vertex shader, `retro.vs`:

| Programa | Fragment shader | Se usa para |
|---|---|---|
| **Lit** | `retro.fs` | Todos los modelos (`.obj` y planos). Es el único con `useLighting = 1`. |
| **Unlit** | `retro_unlit.fs` | Líneas de depuración y marcos de triggers (color de vértice, sin textura). |
| **Sprite** | `retro_sprite.fs` | Los *billboards*. La luz se calcula en la CPU (ver §4.5). |

`RetroShader` guarda los parámetros comunes (`RetroShaderParams`) y los sube a los tres
programas con `SetParams`:

| Parámetro | Por defecto | Qué es |
|---|---|---|
| `snapResolution` | 320x240 | Rejilla a la que se redondean los vértices. |
| `colorLevels` | 32 | Niveles por canal (32 = 15 bits de color). |
| `ditherStrength` | 1.0 | Intensidad de la trama Bayer. |
| `lightDirection` | (-0.5, -1, -0.3) | Hacia dónde viaja la luz direccional. |
| `ambient` | 0.35 | Luz mínima. |
| `sun` | 1.0 | Fuerza de la luz direccional (0 en interiores y bajo tierra). |
| `fogColor` | color de fondo | Se actualiza al cambiar el fondo. |
| `fogStart` / `fogEnd` | 10 / 26 | Distancias de la niebla. |
| `fogEnabled` | sí | Si no, la niebla se manda a un millón de unidades. |
| `pointLights` | sí | Si no, no se mandan luces puntuales (tecla 6). |

### 3.2 Vertex shader (`retro.vs`)

Para cada vértice:

1. **Iluminación por vértice** (solo el programa Lit, ver §4).
2. Se calcula la posición en *clip space* (`mvp * vértice`) y se guarda `w` como **profundidad
   para la niebla**, *antes* de redondear.
3. **Temblor de vértices** (*vertex snapping*): la posición en coordenadas normalizadas de
   pantalla se redondea a una rejilla de `snapResolution / 2` celdas por unidad, es decir, a
   píxeles de 320x240:

   ```
   ndc = floor(ndc * resolución/2 + 0.5) / (resolución/2)
   ```

   Por eso los polígonos "saltan" de píxel en píxel al moverse, como en la PS1. Con la tecla 1
   la rejilla pasa a 4096x4096 y el efecto desaparece.
4. Se pasan las UV con **perspectiva correcta**. No se imita el mapeo afín de la PS1 (UV
   interpoladas en pantalla): doblaba las texturas de los polígonos grandes vistos de cerca.

### 3.3 Fragment shader de modelos (`retro.fs`)

1. **Textura** con las UV en perspectiva correcta: no se deforma aunque te pegues a una pared.
2. **Recorte por alfa**: si `alfa de la textura × alfa del tinte < 0.5`, el píxel se descarta.
   No hay semitransparencia, así que no hace falta ordenar nada.
3. **Color**: `textura × tinte × color de vértice × luz del vértice`.
4. **Niebla**: `f = clamp((w - inicio) / (fin - inicio), 0, 1)` y el color se mezcla con el de
   la niebla.
5. **Cuantización con *dithering***:

   ```
   pasos  = colorLevels - 1                    (31)
   umbral = (Bayer4x4[píxel] + 0.5) / 16 - 0.5  (entre -0.47 y +0.47)
   color  = floor(color * pasos + 0.5 + umbral * fuerza) / pasos
   ```

   La fuerza del *dithering* se reduce con la niebla (`fuerza × (1 - f)`), para que lo lejano no
   se llene de trama. Las teclas 2 y 3 pasan a 256 niveles y quitan la trama.

`retro_sprite.fs` hace lo mismo sin la luz del vértice (los sprites traen su luz en el color) y
`retro_unlit.fs` hace niebla y cuantización sobre el color del vértice.

### 3.4 Planos procedurales y subdivisiones

`"model": { "type": "plane", "size": 40, "subdivisions": 40, "tiles": 20 }` genera con
`GenMeshPlane` un plano cuadrado de `size` de lado, centrado en el origen, en el plano XZ, con
`subdivisions × subdivisions` celdas, y multiplica sus UV por `tiles` (para repetir la textura
con `"wrap": "repeat"`).

Las subdivisiones importan por dos motivos: la iluminación es **por vértice** (un plano de un
solo cuadrado recibe la luz de una farola solo en sus 4 esquinas) y la luz se interpola en
pantalla (`noperspective`), algo que se nota más cuanto más grande es el polígono. Más
subdivisiones = luz más precisa.

### 3.5 Post-proceso (`post.fs`) y presentación

La textura de 320x240 se dibuja en la ventana dentro de un rectángulo que conserva la proporción
(con bandas negras si hace falta; `integerScaling` permitiría escalar solo por enteros, está
desactivado). El shader de post-proceso:

1. **Escalado** (tecla F7 para cambiar):
   - `0` píxeles duros: vecino más cercano.
   - `1` **bilineal nítido** (por defecto): dentro de cada píxel de origen el color es plano, y
     solo en una franja de `1/escala` en el borde se mezcla con el vecino. Los píxeles quedan
     cuadrados y del mismo tamaño aunque la escala no sea entera, sin el emborronado del
     bilineal.
   - `2` bilineal normal.
2. **Tinte** (por defecto `1.04, 1.0, 0.93`, un poco cálido) y **saturación** (1.05).
3. **Viñeta**: oscurece hasta un 30 % hacia las esquinas
   (`smoothstep(0.30, 0.78, distancia al centro)`).
4. **Grano**: ruido de ±0.035 que cambia 24 veces por segundo, en bloques de 2x2 píxeles de
   ventana.

F8 quita tinte, saturación, viñeta y grano, dejando solo el escalado.

### 3.6 Sprites (*billboards*)

`DrawUprightBillboard` dibuja un cuadrado con la base centrada en la posición de la entidad
(los pies), orientado hacia la cámara **solo en horizontal**: el sprite nunca se inclina aunque
lo mires desde arriba. Su tamaño es `size × |scale|` (X para el ancho, Y para el alto). Los
sprites ignoran la rotación. No se ordenan por distancia gracias al recorte por alfa.

---

## 4. Luces

Hay tres tipos de luz:

1. **Una luz direccional** por escena (`"light": { "direction", "ambient", "sun" }`), blanca.
   `sun` (0..1, por defecto 1) es su fuerza: con `0` solo queda el ambiente y las luces
   puntuales, que es lo que necesitan los interiores (la cripta, los túneles); si no, el "sol"
   iluminaría las paredes bajo tierra.
2. **Luces puntuales de escena** (`"lights": [ ... ]`).
3. **Luces puntuales de entidad** (`"light"` dentro de una entidad).

### 4.1 Cálculo por vértice (Gouraud)

En `retro.vs`, para cada vértice de un modelo:

```
normal = normalizar(transpuesta(inversa(matModel)) * normal)   // correcta con escala no uniforme
difusa = max(dot(normal, -normalizar(direccion)), 0)
luz    = ambiente + (1 - ambiente) * sol * difusa               // luz direccional, blanca

para cada luz puntual i (hasta 8):
    d    = distancia(vértice, luz_i)
    at   = clamp(1 - d / radio_i, 0, 1)
    ndl  = max(dot(normal, dirección hacia la luz), 0)
    luz += color_i * at² * ndl
```

La luz resultante se interpola entre vértices con `noperspective` (como la PS1) y multiplica al
color de la textura. Las luces se **suman**: varias luces juntas pueden saturar a blanco (el
resultado se recorta a 1 al final).

### 4.2 Color e intensidad

En el JSON una luz tiene `color` (0–255) e `intensity`. El cargador las combina en un solo
color: `color / 255 × intensity`. Una intensidad de 1.3 hace una luz más fuerte que el blanco.

### 4.3 Parpadeo (`flicker`)

`flicker` (0 a 1) y `flickerRate` (cambios por segundo, por defecto 10). Cada fotograma:

```
paso   = floor(tiempo * flickerRate) + fase * 7919
ruido  = hash(paso)                       // pseudoaleatorio entre 0 y 1
factor = 1 - flicker * ruido
color  *= factor
radio  *= 0.85 + 0.15 * factor
```

La luz salta de un valor a otro `flickerRate` veces por segundo (no es suave, es un parpadeo de
bombilla). La **fase** hace que dos luces no parpadeen a la vez: para las de escena es su
índice; para las de entidad, un *hash* del nombre de la entidad.

### 4.4 Cuáles se usan: las 8 más cercanas

El shader admite **8 luces puntuales** (`kMaxPointLights`). Cada fotograma
(`SceneManager::Draw`):

1. `Scene::CollectLights` junta las luces de escena y las de las entidades **visibles**, con el
   parpadeo ya aplicado. La posición de una luz de entidad es
   `posición + offset × scale` (componente a componente: el offset se escala pero **no gira**
   con la entidad).
2. `KeepNearestLights` se queda con las 8 de menor `distancia a la cámara - radio`. Restar el
   radio hace que una luz grande y algo lejana pueda ganar a una pequeña y cercana.
3. Se suben al shader Lit. Con la tecla 6 se desactivan.

Si una escena tiene más de 8 luces, al moverse se verá a las más lejanas "encenderse" cuando
entran en el grupo de 8.

### 4.5 Luz de los sprites

Los sprites no pasan por el cálculo por vértice. `Scene::Draw` calcula **un color por sprite**
en la CPU, medido en el centro del sprite:

```
base  = ambiente + (1 - ambiente) * 0.5 * sol
color = (base, base, base)
para cada una de las 8 luces elegidas:
    t      = clamp(1 - distancia / radio, 0, 1)
    color += color_luz * t² * 0.6
```

y lo multiplica por el tinte. Un sprite con `"emissive": true` ignora todo esto y se ve a pleno
color (farolas, ventanas encendidas). La luz direccional no afecta a los sprites más allá del
término `base`.

---

## 5. Entidades

Una entidad (`src/world/Entity.hpp`) es **exactamente uno** de:

- **Modelo**: un `.obj` o un plano procedural. Puede tener varios materiales; los que traen
  textura en su `.mtl` (madera, tejas...) la conservan, y la textura opcional de la entidad
  (`"texture"`) va a los materiales **sin textura propia**. Un modelo sin `.mtl` (o un plano)
  tiene un solo material sin textura, así que `"texture"` lo cubre entero. Si la entidad no da
  `"texture"`, esos materiales quedan en blanco (no heredan la de otra entidad con el mismo
  modelo).
- **Sprite**: una textura dibujada como *billboard* con un tamaño `[ancho, alto]`.

### 5.1 Transformación

`Transform` = posición, rotación (grados, Euler XYZ) y escala. La matriz es
`escala → rotación XYZ → traslación`. Los modelos comparten la malla, así que justo antes de
dibujar cada entidad se pone su matriz en `model.transform` y su textura en los materiales.

### 5.2 Componentes y comportamientos

| Campo | Qué hace |
|---|---|
| `tint` | Multiplica el color. Su alfa participa en el recorte por alfa (por debajo de 128 no se dibuja). |
| `visible` | Si es `false`, la entidad no se dibuja, **no choca**, no se puede interactuar con ella y su luz y su sonido se apagan. |
| `showIf` | Condiciones (§9). Si las tiene, `visible` se recalcula cada fotograma: visible solo si se cumplen todas. |
| `spin` | Grados por segundo en X, Y, Z sumados a la rotación cada fotograma. Cambia la rotación real, así que **la caja de colisión gira con ella**. |
| `facePlayer` | Si el jugador está a menos de `range` (por defecto 10) en horizontal, la entidad gira en Y hacia él a `turnSpeed` grados por segundo (por defecto 240), por el camino más corto. |
| `idle` | Balanceo de "respiración": un bote vertical `|sin(t)| × bob` y un balanceo lateral (giro en Z) `sin(t/2) × sway` grados, con `t = tiempo × speed × 3 + fase`. Es **solo visual**: no mueve la colisión ni el punto de interacción. La fase sale del nombre, así dos aldeanos no se mueven igual. |
| `light` | Luz puntual propia (§4). |
| `sound` | Emisor de sonido en bucle propio (§12). |
| `collider` | Caja de colisión (§6). |
| `interact` | Qué pasa al pulsar E mirándola (§11). |

### 5.3 Caja local (`LocalBounds`)

Usada por el editor para la selección y el enfoque: para un modelo, la unión de las cajas de
todas sus mallas en espacio local (`GetMeshBoundingBox` por malla; no se usa
`GetModelBoundingBox` porque aplicaría el `model.transform` de la última entidad que dibujó esa
malla compartida). Para un sprite, `ancho × alto × (0.4 × ancho)` con la base en Y = 0.

---

## 6. Colisiones y colisión automática

### 6.1 Las piezas

- **`BoxCollider`**: una caja en el espacio **local** de la entidad (`min`, `max`), antes de
  aplicar su transformación.
- **`WorldBox`**: la caja ya en el mundo. Es un rectángulo **girado solo en el plano XZ** (dos
  ejes 2D, `axisX` y `axisZ`, y dos semilados) más un rango vertical `[bottom, top]`.
- **`MakeWorldBox(collider, transform)`**: construye la `WorldBox` con **la misma matriz con la
  que se dibuja la entidad**, de modo que colisión y modelo siempre coinciden:
  - el centro es el centro local transformado por la matriz;
  - los ejes son las columnas X y Z de la matriz proyectadas al plano XZ y normalizadas;
  - los semilados son los semilados locales por la longitud de esos ejes (la escala);
  - la altura es `position.y + min.y × scale.y` hasta `position.y + max.y × scale.y`.

  Limitación: la altura no tiene en cuenta los giros en X o Z. Un objeto inclinado sigue teniendo
  una caja vertical.

### 6.2 De dónde salen las cajas de una entidad

Una entidad puede tener **varias cajas** (`Entity::colliders`), y cada caja puede ir girada
sobre su eje vertical (`BoxCollider::yaw`, alrededor de su propio centro). Con cuatro franjas
giradas 0°, 45°, 90° y 135° se forma un octógono exacto: así chocan la fuente, el pozo y la
torre del molino.

El campo `collider` de cada entidad decide:

| Valor de `collider` | Resultado |
|---|---|
| `true` o `"auto"` | Cajas automáticas (ver abajo). |
| `false` | Sin colisión: se atraviesa. |
| `{ "min": [x,y,z], "max": [x,y,z], "yaw": 0 }` | Una caja personalizada en espacio local (`min` < `max` en los tres ejes; `yaw` opcional). |
| ausente | Depende de **`autoCollide`** de la escena. |

**Colisión automática** (`"autoCollide": true` en la raíz de la escena, por defecto `false`):
una entidad **sin** campo `collider` recibe la caja automática **solo si su modelo es un archivo
`.obj`**. Los planos procedurales nunca la reciben (el suelo ya es el plano Y = 0, y una caja
plana del tamaño del suelo bloquearía todo) y los sprites tampoco (un árbol con colisión
automática sería tan ancho como su copa); a un sprite hay que ponerle `"collider": true` o una
caja.

**Las cajas automáticas** se calculan al cargar:

- Para un modelo que **trae sus propias cajas**: esas. Los modelos hechos con
  `tools/blender/` las llevan dentro del `.obj`, en comentarios que raylib ignora:

  ```
  # retro:col min_x min_y min_z max_x max_y max_z yaw
  ```

  Solo cubren lo que estorba a la altura del jugador (paredes, postes, pilas, bancos), no el
  vuelo del tejado ni el aire de alrededor. Se definen en el script de cada modelo con
  `Malla.colision()` y `Malla.octogono()`.
- Para un modelo sin cajas propias: la unión de las cajas de todas sus mallas en espacio local,
  es decir, una caja que envuelve todo el modelo.
- En los dos casos, `MakeWorldBox` les aplica posición, giro en Y y escala de la entidad cada
  fotograma.
- Para un sprite: un cuadrado de `0.4 × ancho` de lado en la base, y tan alto como el sprite
  (un "tronco" estrecho, no la copa).

Consecuencias que conviene saber:

- Un modelo **sin** cajas propias tiene una sola caja: si es cóncavo (una casa con porche, un
  arco) es sólido entero. La solución es darle cajas en su script de Blender.
- La caja de interacción por defecto (§11) es la de colisión si la entidad tiene **una**; si
  tiene varias, la de todo el modelo.
- Si la entidad **no es visible** (`visible: false` o `showIf` sin cumplir) su caja no existe.
  No se pueden hacer "paredes invisibles" con una entidad oculta.
- Los límites `bounds` sí son paredes invisibles (§6.4).

### 6.3 Pruebas geométricas (`src/physics/Collision.cpp`)

Todas trabajan pasando el punto al espacio local de la caja (proyectándolo sobre sus dos ejes):

- **`OverlapsCircle(caja, punto, radio)`**: distancia del punto al rectángulo (restando la parte
  que cae dentro) menor que el radio.
- **`PushOutCircle`**: si el círculo se solapa, lo saca:
  - si su centro está fuera de la caja, lo empuja a lo largo de la línea entre el punto más
    cercano del rectángulo y el centro, justo lo que sobra (`radio - distancia`);
  - si el centro está dentro, lo saca por la cara más cercana.
- **`RayIntersectsBox`**: rayo contra la caja orientada con el método de *slabs* (X y Z locales,
  Y del mundo). Devuelve la distancia al primer punto, o 0 si el origen está dentro.

### 6.4 Límites de la escena (`bounds`)

`"bounds": { "min": [x, z], "max": [x, z] }` define el rectángulo jugable. Se convierte en
**cuatro muros invisibles** por fuera del rectángulo, de 2 unidades de grosor y de Y = -1 a
Y = 10 (más alto que cualquier salto). Los muros norte y sur se alargan para cubrir las esquinas.
Son mucho más gruesos que lo que avanza el jugador en un subpaso, así que no se pueden atravesar.
El cargador rechaza la escena si el punto de inicio del jugador está fuera de `bounds`.

### 6.5 El movimiento contra las cajas

Ver §7.3: el jugador es un cilindro de radio 0.3 y altura 1.7. Cada caja se trata, según su
altura respecto a los pies del jugador, como **pared**, **escalón o plataforma** (se pisa) o
**techo lejano** (se ignora).

---

## 7. El jugador

### 7.1 Parámetros (`PlayerSettings`)

| Parámetro | Valor |
|---|---|
| Radio del cilindro | 0.3 |
| Altura | 1.7 |
| Altura de los ojos | 1.55 |
| Escalón máximo | 0.35 (se sube sin saltar) |
| Velocidad andando / corriendo | 3.5 / 6.0 por segundo |
| Altura de salto | 0.9 |
| Gravedad | 20 |
| Sensibilidad del ratón | 0.12 grados por píxel |
| Inclinación máxima | ±85 grados |

### 7.2 Control

- El ratón cambia yaw e inclinación. Al activar el jugador se ignoran los 2 primeros fotogramas
  de ratón (el salto del cursor al capturarlo).
- W/S y A/D forman la dirección deseada en el plano, normalizada (en diagonal no se va más
  rápido).
- La velocidad horizontal **acelera hacia la deseada** con un límite de 60 unidades/s² en el
  suelo y 12 en el aire: arranques y frenadas rápidos pero no instantáneos, y poco control en el
  aire.
- El salto (solo en el suelo) da una velocidad vertical `sqrt(2 × gravedad × altura)` = 6.
- El paso de tiempo se limita a 0.05 s y se divide en **subpasos de como mucho 1/120 s**.

### 7.3 Un subpaso (`Player::Step`)

**Horizontal**:

1. Se mueve la posición XZ con la velocidad.
2. Hasta 3 pasadas (para resolver esquinas): para cada caja,
   - si `pies + escalón >= techo de la caja`, se ignora (se puede pisar);
   - si `pies + altura <= suelo de la caja`, se ignora (está por encima de la cabeza);
   - si no, es una **pared**: `PushOutCircle` saca al jugador. Esto da el deslizamiento natural a
     lo largo de las paredes, porque solo se corrige la componente que entra en la caja.

**Vertical**:

1. Se aplica la gravedad y se mueve en Y.
2. El suelo es el plano Y = 0 o el **techo más alto** de las cajas que el jugador tiene debajo
   (su círculo se solapa con la caja) y que están como mucho un escalón por encima de los pies
   del subpaso anterior.
3. Si los pies quedan por debajo de ese suelo, se colocan encima, la velocidad vertical vuelve a
   0 y el jugador está "en el suelo".

Así funcionan:

- **Escalones**: una caja de hasta 0.35 por encima de los pies se ignora como pared y se
  convierte en suelo: el jugador sube de golpe al pisarla.
- **Plataformas y tejados**: cualquier caja es pisable si se llega a su techo (saltando o desde
  otra caja).
- **Caídas**: al salir de una caja el suelo vuelve a ser más bajo y la gravedad actúa.

Limitaciones: no hay colisión con techos (al saltar contra la parte de abajo de una caja, se
trata como pared y el jugador es empujado a un lado), no hay rampas (solo escalones) y el plano
Y = 0 existe en todas partes, también fuera del suelo dibujado.

### 7.4 Cámara

En primera persona la cámara se coloca en los ojos con el yaw y la inclinación del jugador. Con
F2 se pasa a una **cámara orbital** alrededor del jugador (arrastrar con el ratón o flechas,
rueda para la distancia); el jugador se queda quieto y no se disparan triggers ni interacciones.

El bloque `camera` del JSON se aplica al cargar, pero en el juego la cámara del jugador lo
sustituye desde el primer fotograma (y F2 centra la órbita en el jugador). En la práctica sirve
como **vista inicial del editor**.

---

## 8. Triggers

Un trigger (`src/world/Trigger.hpp`) es una zona invisible que ejecuta acciones cuando el
jugador entra, sale o está dentro.

### 8.1 La zona

- `position`: el **centro de la base** de la zona (a la altura de los pies).
- `size`: `[ancho X, alto, fondo Z]`, por defecto `[2, 2.4, 0.8]` (el tamaño de una puerta).
- `rotation`: giro en Y, en grados.

Se construye una `WorldBox` igual que las de colisión. El jugador "está dentro" si su
**círculo** (radio 0.3) se solapa con el rectángulo y su cilindro se solapa en altura
(`pies + 1.7 > suelo de la zona` y `pies < techo de la zona`).

### 8.2 Eventos

`"on"`: `"enter"` (por defecto) se dispara el fotograma en que pasa de fuera a dentro; `"exit"`
de dentro a fuera; `"stay"` **todos los fotogramas** que está dentro.

### 8.3 Orden de evaluación (`Scene::UpdateTriggers`)

Para cada trigger, cada fotograma (solo en primera persona y fuera de un fundido):

1. Se calcula si el jugador está dentro ahora y se compara con el fotograma anterior.
2. **El primer fotograma después de cargar la escena solo registra** dónde está el jugador. Si
   apareces dentro de una zona `enter`, no se dispara hasta que salgas y vuelvas a entrar.
3. Si el evento ocurre:
   - si es `once` y ya se usó, se ignora;
   - si sus condiciones `if` no se cumplen, se ignora **sin gastar el `once`**;
   - si es `once`, se marca como usado;
   - sus acciones `do` se añaden a la lista del fotograma.
4. Al final, `ActionRunner` ejecuta todas las acciones disparadas, en orden.

El `once` se guarda en `GameState` como la bandera `once:<ruta de la escena>#<nombre>`, así
sobrevive al cambio de escena y a guardar y cargar. F6 lo reinicia.

### 8.4 Marco visible

Con `"marker": true` el trigger se dibuja como un marco de alambre con dos rectángulos que suben
en bucle, del color `color` (por defecto `[255, 200, 80, 255]`). Se usa para portales.

### 8.5 Patrón típico: dos triggers en el mismo sitio

`demo.json` usa dos triggers en la misma posición para una puerta: `puerta_cerrada` (si **no**
tienes llave y 3 monedas: sonido y mensaje) y `puerta_noche` (si las tienes: sonido y cambio de
escena). Las condiciones son exactamente opuestas, así que solo se dispara uno.

---

## 9. Condiciones

Las condiciones (`src/world/Logic.hpp`) se evalúan sobre las banderas de `GameState`. Una
bandera que nunca se fijó vale **0**.

Cada condición es un objeto con **exactamente una** de estas claves:

```jsonc
{ "flag": "monedas", "op": ">=", "value": 3 }   // comparación
{ "flag": "tiene_llave" }                        // op "==" y value 1 por defecto: "es 1"
{ "all": [ ... ] }                                // todas (Y)
{ "any": [ ... ] }                                // alguna (O)
{ "not": { ... } }                                // negación
```

- Operadores: `==` (por defecto), `!=`, `<`, `<=`, `>`, `>=`.
- `all` y `any` necesitan un arreglo no vacío.
- Se pueden anidar hasta 8 niveles.
- Donde se aceptan condiciones (`if` de triggers e interacciones, `showIf` de entidades) se puede
  poner **un objeto** o **un arreglo de objetos**; en el arreglo deben cumplirse todos. Sin
  condiciones, se cumple siempre.

Atención: `{ "flag": "x", "not": true }` **no es válido** (tiene dos claves). La negación se
escribe `{ "not": { "flag": "x" } }`.

---

## 10. Acciones

Cada acción es un objeto con **exactamente una** clave de acción más sus modificadores. Las
ejecuta `ActionRunner` en orden.

| Acción | Modificadores (valor por defecto) | Efecto |
|---|---|---|
| `{ "goto_scene": "scenes/x.json" }` | `position` [x,y,z], `yaw` | Cambia de escena con fundido. Si se dan, `position` y `yaw` sustituyen el punto de inicio del destino. Si el archivo no existe, se avisa por consola y no pasa nada. |
| `{ "teleport": [x, y, z] }` | `yaw` (el actual) | Mueve al jugador al instante (reinicia su velocidad). |
| `{ "set_flag": "nombre" }` | `value` (1) | Fija la bandera. |
| `{ "add_flag": "nombre" }` | `amount` (1) | Suma a la bandera (puede ser negativo). |
| `{ "message": "texto" }` | `seconds` (3) | Muestra un cuadro de texto abajo. `\n` hace saltos de línea y `{bandera}` se sustituye por su valor: `"Monedas: {monedas}/3"`. Un mensaje nuevo reemplaza al anterior. |
| `{ "play_sound": "audio/x.wav" }` | `volume` (1), `at`, `radius` (14), `minDistance` (1) | Efecto de sonido. Sin `at`, suena "en la cabeza" (sin posición). Con `at: [x,y,z]` o `at: "nombre_de_entidad"`, suena en ese punto con atenuación y paneo (§12.3). |
| `{ "play_music": "audio/x.wav" }` | `volume` (0.6), `fade` (1) | Cambia la música con fundido cruzado. |
| `{ "stop_music": true }` | `fade` (1) | Apaga la música con fundido (el valor de la clave no importa). |

No existe una acción `set_visible`: la visibilidad se controla con banderas y `showIf` (por
ejemplo, la llave tiene `"showIf": { "not": { "flag": "tiene_llave" } }` y el trigger que la
recoge hace `set_flag tiene_llave`).

---

## 11. Interacción (tecla E)

```jsonc
"interact": {
  "prompt": "Hablar",        // texto tras "E: " (por defecto "Interactuar")
  "range": 2.5,              // distancia maxima (por defecto 2.5)
  "once": false,             // solo una vez (por defecto no)
  "box": { "min": [...], "max": [...] },   // opcional
  "if": { ... },             // opcional
  "do": [ ... ]              // obligatorio, al menos una accion
}
```

Cada fotograma (solo en primera persona y fuera de un fundido), `Scene::FindInteractable`:

1. Lanza un rayo desde la **cámara** (los ojos) en la dirección en que se mira.
2. Para cada entidad visible con `interact`, que no haya gastado su `once` y cuyas condiciones
   se cumplan, prueba el rayo contra su **caja de interacción**:
   - `box` si se indicó;
   - si no, su caja de colisión;
   - si no tiene colisión, la caja automática del modelo o del sprite.
3. Descarta las que estén más lejos que `range` (medido a lo largo del rayo hasta la caja) y se
   queda con la más cercana.
4. **Oclusión**: si la caja de colisión de *otra* entidad visible corta el rayo antes, no se
   puede interactuar (no se habla con alguien a través de una pared). La propia entidad no se
   tapa a sí misma.

Si hay candidata, la mira se pone amarilla y aparece `E: <prompt>`. Al pulsar **E** se ejecutan
sus acciones; con `once`, se marca la bandera `interact:<escena>#<nombre>`.

Truco usado en las monedas: una `box` mucho más grande que el objeto (`[-1.5..1.5]`) para que
sea fácil apuntar a algo pequeño.

---

## 12. Audio

`AudioSystem` (`src/audio/AudioSystem.cpp`). Si no hay dispositivo de audio, todas sus funciones
no hacen nada. Si un archivo falla, avisa **una vez** por consola y sigue.

Formatos: wav, ogg, mp3, flac (lo que soporte Raylib).

### 12.1 Música y ambiente

Dos canales de *streaming* independientes (`music_` y `ambient_`), cada uno con una pista
actual y una saliente:

- Si se pide **la misma pista** que ya suena, solo cambia su volumen objetivo (no se reinicia).
- Si es **otra**, la actual pasa a "saliente" y baja a 0 en `fade` segundos, mientras la nueva
  empieza en 0 y sube hasta su volumen en `fade` segundos: **fundido cruzado**.
- Una pista vacía apaga el canal con fundido.

Al cargar una escena se aplica su bloque `audio`: `music`, `ambient` y `fade` (por defecto 1.5
s). Una escena sin música hace que la anterior se apague.

### 12.2 Efectos

`PlaySfx`: un efecto cargado entero en memoria (se cachea por ruta). Raylib reinicia un sonido si
se vuelve a reproducir mientras suena, así que **el mismo efecto no se solapa consigo mismo**.

### 12.3 Sonido posicional

El **oyente** es la cámara (posición y dirección horizontal). Para un sonido en un punto:

```
d = distancia(oyente, punto)
si d >= radio: no se oye
t        = clamp((d - minDistance) / (radio - minDistance), 0, 1)
ganancia = volumen * (1 - t)²            // a menos de minDistance, volumen completo

lateral = dot(dirección al sonido, derecha del oyente) * clamp(distancia horizontal / 2, 0, 1)
si el sonido está detrás:  ganancia *= 1 + 0.35 * dot(dirección, delante)   // hasta -35 %
pan = 0.5 - 0.5 * lateral * 0.85
```

El paneo se suaviza a menos de 2 unidades (un sonido justo encima no salta de un oído a otro) y
nunca es total (0.85), para que no suene antinatural. Hay una constante `kFlipPan` por si los
canales salieran invertidos en algún sistema.

`PlaySfxAt` aplica esto una vez al empezar el efecto (un efecto en curso no se reajusta si el
jugador se mueve).

### 12.4 Emisores (sonidos en bucle en el mundo)

Vienen de `"emitters"` de la escena y del `"sound"` de las entidades visibles (en
`posición + offset × scale`; como las luces, el offset no gira). Cada fotograma
(`UpdateEmitters`):

1. Se calcula la ganancia y el paneo de cada emisor con la fórmula de §12.3.
2. Se ordenan por ganancia y se quedan **los 6 más audibles** (`kMaxEmitterVoices`).
3. Cada emisor tiene una **clave** estable (`scene:<índice>` o `entity:<nombre>`). Si no tenía
   voz, se abre un *stream* nuevo y empieza en un **punto aleatorio** del archivo, para que dos
   emisores con el mismo sonido no suenen en fase.
4. El volumen de cada voz se acerca a su objetivo de forma suave (`k = min(1, dt × 8)` por
   fotograma), así no hay saltos al moverse.
5. Las voces que salen del grupo de 6 (o se alejan) bajan a 0 y, cuando son inaudibles, se
   cierran y se liberan.

Al cambiar de escena se cortan todas las voces de emisor.

Requisito del formato: `minDistance` debe ser menor que `radius` (el cargador lo comprueba).

---

## 13. Pasos

Los pasos tienen dos partes: el **contador de distancia** (`FootstepTracker`,
`src/audio/Footsteps.hpp`) y la **reproducción** (`AudioSystem::Footstep`).

### 13.1 Cuándo suena un paso

Cada fotograma, con la posición de los pies:

1. Se mide la distancia **horizontal** recorrida desde el fotograma anterior.
2. No se cuenta si:
   - el jugador **no está en el suelo** (saltando o cayendo);
   - no está activo (cámara orbital o durante un fundido);
   - se movió más de 1 unidad en un fotograma (es un teletransporte o un cambio de escena, no
     un paso).
3. La distancia se acumula. Cuando llega a la **zancada** (1.9 andando, 2.6 con Shift pulsada),
   suena un paso y se resta la zancada (el sobrante se conserva, así el ritmo es regular).

Correr da pasos más espaciados en distancia pero más frecuentes en el tiempo (6 / 2.6 ≈ 2.3
pasos por segundo frente a 3.5 / 1.9 ≈ 1.8).

Al aparecer (cargar escena, cargar partida) el contador se reinicia.

### 13.2 Qué suena

Cada escena define su lista de sonidos de paso en `"audio": { "footsteps": { "files": [...],
"volume": 0.6 } }` (`demo.json` usa piedra; `aldea.json`, pasto). En cada paso:

- se elige uno al azar, **sin repetir el anterior** si hay más de uno;
- se varía el volumen ±12 % y el tono ±8 % para que no suene a máquina;
- suena centrado (sin posición).

Limitación: los pasos son **por escena**, no por superficie. Una escena con suelo de piedra y de
pasto suena igual en los dos.

---

## 14. Escenas y cambio de escena

### 14.1 Qué es una escena en memoria

`Scene` (`src/world/Scene.hpp`) contiene: entidades (en el orden del JSON), triggers, luces,
emisores, límites opcionales, punto de inicio del jugador, bloque de audio y la ruta del archivo.

### 14.2 Carga (`SceneLoader`)

`LoadScene(ruta)` lee y parsea el archivo; `LoadSceneFromJson(json, ruta)` interpreta un
documento ya en memoria (lo usa el editor). El orden es:

1. `camera` → la cámara orbital inicial.
2. `background` → color de fondo y de niebla.
3. `fog` y `light` → parámetros del shader.
4. `player` → punto de inicio.
5. `bounds` (comprueba que el inicio esté dentro).
6. `autoCollide`.
7. `audio` (comprueba que existan los archivos).
8. `lights`.
9. `emitters` (comprueba los archivos).
10. `entities` (**obligatorio**, aunque sea vacío; nombres únicos).
11. `triggers` (nombres únicos; si falta el nombre se usa `trigger_<n>`).

Las claves desconocidas se ignoran. Cualquier error lanza una excepción con el contexto:
`scenes/demo.json (entidad #12 'llave'): 'size' debe ser positivo`.

### 14.3 Carga transaccional (`SceneManager::LoadNow`)

1. Se guardan la cámara, los parámetros del shader y el color de fondo actuales.
2. Se aplica el **entorno por defecto** (los valores con los que arrancó el motor) y una cámara
   nueva: así una escena que no define `fog` no hereda la niebla de la anterior.
3. La escena nueva se construye **aparte**. La anterior sigue viva.
4. Si algo falla, se restauran cámara, shader y fondo, y el error sube. La escena anterior sigue
   intacta y jugable.
5. Si sale bien, la nueva reemplaza a la anterior (que se destruye), se aplica el punto de
   inicio pedido por `goto_scene` (si lo hubo), se aplica el audio de la escena y se liberan los
   assets que ya nadie usa (`PurgeUnused`).

Cada carga correcta incrementa `Generation()`; `main.cpp` lo usa para saber que tiene que
recolocar al jugador y autoguardar.

### 14.4 Cambio con fundido

`GoToPath(ruta, posición, yaw)` (lo usa `goto_scene`, y también cargar partida):

1. Si la ruta no está en la lista de escenas conocidas y el archivo existe, se añade. (Tab y
   Shift+Tab recorren esa lista.)
2. **Fundido a negro** en 0.35 s, en **8 escalones** (no es suave, como los fundidos de la
   época). Durante el fundido el jugador no se mueve y no se evalúan triggers ni interacciones.
3. Con la pantalla en negro se carga la escena (el tirón de la carga no se ve).
4. Si la carga falla, se avisa por consola y se sigue en la escena anterior.
5. Fundido de vuelta.

F5 (`Reload`) recarga la escena actual al instante desde el disco, sin fundido.

---

## 15. Estado del juego y partidas guardadas

### 15.1 `GameState`

- Un mapa de **banderas enteras** (`nombre → int`; las que no existen valen 0). Incluye las
  banderas del juego y las marcas `once:` e `interact:`.
- El **mensaje** en pantalla y su temporizador.

F6 borra todas las banderas (y por tanto los `once`); hay que pulsar F5 para recargar la escena
y ver el efecto en lo que ya estaba cargado.

### 15.2 Formato del guardado

```json
{
  "version": 1,
  "savedAt": "2026-10-17 21:30:00",
  "playTime": 123.4,
  "scene": "scenes/noche.json",
  "player": { "position": [0, 0, 13], "yaw": 0, "pitch": -5 },
  "flags": { "monedas": 3, "once:scenes/demo.json#cartel": 1, "tiene_llave": 1 }
}
```

Las banderas se escriben ordenadas por nombre. Al leer se valida todo: versión (rechaza
guardados de una versión más nueva), escena no vacía, posición con 3 números finitos, banderas
enteras.

### 15.3 Ranuras y carpeta

- Carpeta: `%APPDATA%/RetroEngine/saves` en Windows (`~/Library/Application Support/...` en
  macOS, `$XDG_DATA_HOME` o `~/.local/share/...` en Linux; si no hay variables, junto al
  ejecutable).
- Ranura 0 = `autosave.json`; ranura 1 = `slot1.json` (F9 / F10).

### 15.4 Escritura atómica y copia de seguridad (`SaveStorage.hpp`)

1. Se escribe el contenido completo en `<archivo>.tmp`.
2. Si falla (disco lleno...), se borra el `.tmp` y el guardado anterior queda intacto.
3. Si existía `<archivo>`, se renombra a `<archivo>.bak` (borrando el `.bak` viejo).
4. Se renombra `.tmp` a `<archivo>`.

En cualquier punto de fallo, la partida anterior está o en su sitio o en el `.bak`. Al leer, si
el archivo falta o no es válido, se prueba el `.bak` (y se avisa por consola).

### 15.5 Cuándo se guarda y se carga

- **F9**: guarda en la ranura 1 (no durante un fundido).
- **Autoguardado**: cada vez que se carga una escena (también con Tab y F5) salvo cuando la
  carga viene de "cargar partida", y al salir del juego si cambió algo (escena o banderas).
- **F10 / Shift+F10**: lee la ranura, cambia a su escena con la posición y el yaw guardados y,
  cuando la escena termina de cargar, sustituye todas las banderas por las guardadas y restaura
  inclinación y tiempo de juego. Si la escena no se pudo cargar, avisa.
- Al arrancar, si hay partida, un mensaje lo recuerda.

---

## 16. Assets y caché

### 16.1 Envoltorios RAII (`src/assets/Resources.hpp`)

`TextureResource`, `ModelResource`, `ShaderResource`, `FontResource`, `SoundResource`,
`MusicResource` y `RenderTarget`: cada uno carga en su constructor (y lanza una excepción con la
ruta si falla) y libera en su destructor. No se copian; se pueden mover.

- Las **texturas** usan filtro de punto (texeles nítidos).
- Los **modelos** `.obj` se cargan con Raylib; las texturas que traiga su `.mtl` (rutas
  relativas a la carpeta del `.obj`, como `../textures/mat_madera.png`) pasan a ser del modelo y
  también usan filtro de punto. `OpenMaterials()` lista los materiales sin textura propia, los
  que reciben la `"texture"` de la entidad. A cada modelo se le asigna el shader Lit.
- Antes de cargar un `.obj` se comprueba que ninguna cara tenga más de 5 vértices: el lector de
  raylib (tinyobj) abortaría el programa entero. Así es un error normal con la línea del
  archivo.
- Los **sonidos** (`Sound`) se cargan enteros; la **música** y los emisores (`Music`) se leen
  por *streaming*.

### 16.2 `AssetManager`

Cachés por ruta relativa con `shared_ptr`: pedir dos veces la misma textura devuelve el mismo
objeto. `PurgeUnused` libera lo que solo la caché tiene (se llama tras cada cambio de escena) y
`Clear` lo vacía todo.

Consecuencia: el `wrap` de una textura se aplica al objeto compartido, así que si dos entidades
usan la misma textura con distinto `wrap`, gana la última que se cargó. Los sprites siempre
fuerzan `clamp`.

### 16.3 Dos carpetas de assets

- El **juego** lee `build/bin/assets/`, una copia que el destino `CopyAssets` de CMake sincroniza
  en cada compilación.
- El **editor** lee y escribe `assets/` del proyecto.

---

## 17. Interfaz en pantalla

`Ui` dibuja texto y rectángulos **a la resolución de la ventana**, después del post-proceso (el
texto queda nítido, sin temblor ni cuantización). Las coordenadas se dan en una rejilla de
referencia de **320 de ancho** (la altura sale de la proporción), así el diseño no cambia si se
cambia la resolución interna. La fuente es `fonts/ui.ttf` cargada a 48 px; si no existe, se usa
la de Raylib.

El cuadro de mensajes va centrado abajo, con fondo negro semitransparente y borde blanco; cada
`\n` es una línea.

---

## 18. Referencia completa del formato de escena

Todos los campos, con su valor por defecto. Solo `entities` es obligatorio en la raíz.

### 18.1 Raíz

| Clave | Por defecto | Notas |
|---|---|---|
| `camera` | posición (5, 4, 5), mira a (0, 0.5, 0), `fov` 60 | `{ "position", "target", "fov" }`. En el juego la sustituye la cámara del jugador; es la vista inicial del editor (§7.4). |
| `player` | `position` [0, 0, 9], `yaw` 0 | Punto de inicio (los pies). |
| `autoCollide` | `false` | §6.2. |
| `bounds` | ninguno | `{ "min": [x, z], "max": [x, z] }`. §6.4. |
| `background` | `[24, 20, 32]` | También es el color de la niebla. |
| `fog` | `start` 10, `end` 26 | |
| `light` | `direction` [-0.5, -1, -0.3], `ambient` 0.35, `sun` 1 | Luz direccional (`sun` entre 0 y 1). |
| `audio` | ninguno | `music` y `ambient`: ruta o `{ "file", "volume" }` (volumen por defecto 0.6 y 0.5). `footsteps`: `{ "files": [...], "volume": 0.6 }`. `fade`: 1.5. |
| `lights` | `[]` | Luces puntuales (§18.4). |
| `emitters` | `[]` | `{ "file", "position", "volume": 1, "radius": 10, "minDistance": 1 }`. |
| `entities` | — | Obligatorio. |
| `triggers` | `[]` | |

### 18.2 Entidad

| Clave | Por defecto | Notas |
|---|---|---|
| `name` | — | Obligatorio y único. |
| `model` | — | `"models/x.obj"` o `{ "type": "plane", "size": 10, "subdivisions": 1, "tiles": 1 }`. Exactamente uno de `model` o `sprite`. |
| `texture` | — | Solo modelos. Va a los materiales sin textura propia en el `.mtl` (§5). |
| `wrap` | el de Raylib | `"repeat"`, `"clamp"` o `"mirror"`. |
| `sprite` | — | Ruta de la textura del sprite. |
| `size` | — | Obligatorio en sprites: `[ancho, alto]`, positivos. |
| `emissive` | `false` | Solo sprites: ignora la luz. |
| `position` | `[0, 0, 0]` | En sprites, el punto bajo los pies. |
| `rotation` | `[0, 0, 0]` | Grados. Los sprites la ignoran al dibujarse. |
| `scale` | `[1, 1, 1]` | |
| `tint` | `[255, 255, 255, 255]` | |
| `visible` | `true` | |
| `showIf` | ninguna | Condición o arreglo (§9). |
| `spin` | `[0, 0, 0]` | Grados por segundo. |
| `collider` | según `autoCollide` | `true`, `false`, `"auto"` o `{ "min", "max" }` (§6.2). |
| `light` | ninguna | `{ "offset": [0,0,0], "color", "intensity", "radius", "flicker", "flickerRate" }` (§18.4). |
| `sound` | ninguno | `{ "file", "offset": [0,0,0], "volume": 1, "radius": 10, "minDistance": 1 }`. |
| `interact` | ninguna | §11. |
| `facePlayer` | no | `true` o `{ "range": 10, "turnSpeed": 240 }`. |
| `idle` | no | `true` o `{ "bob": 0.015, "sway": 1.2, "speed": 2.0 }`. |

### 18.3 Trigger

| Clave | Por defecto | Notas |
|---|---|---|
| `name` | `trigger_<n>` | Único. |
| `position` | `[0, 0, 0]` | Centro de la base. |
| `size` | `[2, 2.4, 0.8]` | Positivos. |
| `rotation` | `0` | Giro en Y, grados. |
| `on` | `"enter"` | `"enter"`, `"exit"`, `"stay"`. |
| `once` | `false` | |
| `marker` | `false` | Dibujar marco animado. |
| `color` | `[255, 200, 80, 255]` | Color del marco. |
| `if` | ninguna | Condición o arreglo. |
| `do` | — | **Obligatorio** (puede ser `[]`). |

### 18.4 Luz puntual

| Clave | Por defecto | Notas |
|---|---|---|
| `position` | `[0, 0, 0]` | Solo en `lights` de la escena. En entidades se usa `offset`. |
| `color` | `[255, 255, 255]` | |
| `intensity` | `1` | Multiplica el color. |
| `radius` | `8` | Positivo. La luz llega a 0 en el radio. |
| `flicker` | `0` | 0 a 1. |
| `flickerRate` | `10` | Cambios por segundo, positivo. |

### 18.5 Ejemplo mínimo

```json
{
  "player": { "position": [0, 0, 5], "yaw": 0 },
  "autoCollide": true,
  "entities": [
    { "name": "suelo", "model": { "type": "plane", "size": 20, "subdivisions": 20, "tiles": 10 },
      "texture": "textures/suelo.png", "wrap": "repeat" },
    { "name": "casa", "model": "models/casa.obj", "texture": "textures/casa_tex.png",
      "position": [0, 0, -3] }
  ],
  "triggers": [
    { "name": "bienvenida", "position": [0, 0, 3], "size": [4, 2, 1], "once": true,
      "do": [ { "message": "Bienvenido" } ] }
  ]
}
```

---

## 19. El editor

`RetroEditor` (`editor/`) usa el mismo `RetroEngine` (ventana de 1600x900, sin fuente de
interfaz propia) y el mismo cargador de escenas que el juego, pero tiene su propio bucle: no usa
`RetroEngine::Run`, ni el jugador, ni el audio.

### 19.1 Modelo de datos

- **`doc_`** (un `nlohmann::ordered_json`) es la **fuente de verdad**: lo que se ve en el
  inspector, lo que se guarda.
- **`scene_`** es solo una **vista previa**: se reconstruye desde `doc_` con
  `LoadSceneFromJson`. Nunca se modifica a mano.
- **`savedDoc_`** es el documento tal como está en el disco: hay cambios sin guardar si
  `doc_ != savedDoc_` (deshacer hasta el estado guardado quita el asterisco).
- **`lastGood_`** es el último documento que construyó una escena válida.

Como el JSON es la fuente de verdad, todo lo que el editor no sabe editar (la lógica, claves
desconocidas) se conserva intacto. Y como la vista previa se construye con el cargador real, el
editor nunca acepta un cambio que el juego rechazaría.

Al reconstruir, el editor pone **todas las entidades visibles**, para poder ver y colocar las que
en el juego dependen de banderas.

### 19.2 Un fotograma del editor

1. Operaciones diferidas del fotograma anterior (duplicar, eliminar).
2. Menú, *dockspace*, Jerarquía, Inspector, Assets, Mensajes, Validacion, Visor (que también
   gestiona el ratón: cámara, gizmo, selección, soltar assets), diálogos, atajos.
3. `FlushChanges`: si algo cambió, reconstruir la escena **una sola vez**.

### 19.3 Protocolo de cambios

Todo widget que modifica el documento sigue este protocolo:

```
BeginChange(id)     // la primera vez en el fotograma, guarda una copia del documento previo
...escribe en doc_...
MarkChanged(id)     // marca que hay que reconstruir
```

Al final del fotograma, `FlushChanges`:

1. Reconstruye la escena desde `doc_`.
2. Si **falla**, restaura `doc_ = lastGood_` y escribe el error en Mensajes
   ("Cambio descartado: ..."). Escribir un valor inválido nunca rompe el editor.
3. Si funciona, apunta la copia previa en el historial de deshacer (salvo que se funda, ver
   abajo), vacía el de rehacer y actualiza `lastGood_`.

**Fundir cambios continuos**: un widget continuo (arrastrar un número, el gizmo) usa su propio
`id`. Si el cambio viene del mismo `id` que la entrada anterior y han pasado menos de 0.8 s, no
se crea entrada nueva: arrastrar un valor durante dos segundos es un solo paso de deshacer. Al
soltar, se marca el fin del gesto y el siguiente arrastre empieza entrada nueva. Los widgets
discretos (casillas, desplegables, texto, botones) usan `id` 0 y nunca se funden.

**Deshacer y rehacer** guardan instantáneas completas del documento, hasta **200** (se descartan
las más antiguas). No funcionan mientras se está editando un campo.

**Operaciones diferidas**: duplicar y eliminar insertan o borran elementos de un arreglo, lo que
invalida los punteros a `doc_` que los widgets están usando ese fotograma; por eso se hacen al
principio del fotograma siguiente.

**El índice manda**: la entidad `i` de `doc_["entities"]` es la entidad `i` de la escena
construida (el cargador no salta ninguna). La selección, el picking y el gizmo dependen de eso.

### 19.4 Campos del inspector

Cada campo (`FieldVec3`, `FieldFloat`, `FieldColor`, `FieldFile`...) lee el valor del JSON (o
el de por defecto si falta), lo muestra y, si cambia, lo escribe con el protocolo. Con
`eraseDefault`, un valor igual al de por defecto **se quita** del archivo en vez de escribirse,
para que el JSON no se llene de valores redundantes. Los números se guardan como enteros si lo
son y, si no, redondeados a 3 decimales.

Los campos de texto se confirman al terminar de escribir (no en cada letra); pueden ser de
varias líneas (el texto de los mensajes). Los desplegables de archivo muestran en rojo, con
"(no existe)", un valor que no está en la lista (un archivo que no está en assets, o una
entidad que no existe), y aceptan que se les suelte un asset arrastrado.

### 19.5 Qué se edita

- **Entidad**: nombre, visible; imagen, tamaño y emisivo (sprite) o modelo / parámetros del
  plano, textura y repetición (modelo); posición, rotación (no en sprites), escala, tinte, giro;
  colisión (según la escena / sí / no / caja personalizada, editada como centro y tamaño, con
  "Ajustar al modelo"); luz
  propia (añadir, offset, color, intensidad, radio, quitar); sonido propio (añadir, archivo,
  offset, volumen, radio, distancia mínima, quitar); "Visible solo si" (`showIf`) e
  interacción (§19.12). El resto de claves (`facePlayer`, `idle`...) aparece en "Otros (solo
  lectura)".
- **Trigger**: nombre, posición, tamaño, giro, evento, una sola vez, marco y su color;
  condiciones (`if`) y acciones (`do`) (§19.12).
- **Luz**: posición, color, intensidad, radio, parpadeo y cambios por segundo.
- **Sonido**: archivo, posición, volumen, radio, distancia mínima.
- **Escena** (sin nada seleccionado): cámara inicial (con "Usar la vista actual"), inicio del
  jugador (con "Inicio en el centro de la vista"), colisión automática, fondo, niebla, luz
  direccional (dirección, ambiente y fuerza del sol), límites (activar, editar, quitar) y **audio**: música y ambiente
  (añadir, archivo, volumen 0-1, quitar), pasos (volumen y lista de sonidos: añadir, cambiar,
  quitar) y fundido al cambiar de escena. Una pista escrita con el atajo `"music": "ruta"`
  se muestra con un botón "Editar volumen" que la pasa a `{ file, volume }`. Lo desconocido,
  en "Otros (solo lectura)".

### 19.6 Visor

- **Vista limpia** (por defecto): sin temblor, 256 niveles de color, sin *dithering*, a la
  resolución del panel.
- **Vista PS1** (menú Ver): se dibuja a 320x240 y se presenta con el post-proceso del juego,
  con bandas negras para mantener la proporción.
- **Capas** (menú Ver): rejilla, colisiones (rojo), zonas de trigger (verde, o violeta si tienen
  marco), luces y sonidos (esfera y círculo del radio), límites (naranja), punto de inicio
  (cilindro del jugador con una línea hacia donde mira), niebla, y "animar la escena" (ejecuta
  `spin`, `facePlayer` mirando a la cámara del editor e `idle`; solo en la vista previa, sin
  tocar el JSON).
- La selección se resalta con su caja en amarillo, su caja de colisión en naranja y, si tiene
  `interact`, su caja de interacción en cian.

**Cámara** (`EditorCamera`): con el botón derecho pulsado se captura el ratón para mirar y se
vuela con W A S D (Q/E bajar y subir, Shift ×3, Ctrl ×0.25, rueda cambia la velocidad entre 1 y 150; empieza en 12 m/s). Sin
el botón derecho, la rueda acerca o aleja 2.5 unidades por paso y el botón central desplaza la
vista. F enfoca la selección (se aleja 2.5 veces su radio, mínimo 3).

### 19.7 Selección (picking)

Un clic lanza un rayo desde el ratón (en la vista PS1, dentro del rectángulo 4:3) y recoge todo
lo que toca:

- entidades: contra su caja local orientada con su matriz;
- triggers (si su capa está visible): contra su zona;
- luces y sonidos (si su capa está visible): contra una esfera de 0.35.

Los resultados se ordenan por **prioridad** y luego por distancia: primero objetos, luces y
sonidos; luego triggers; y al final las entidades **planas** (altura menor que 0.15, como el
suelo), para que el suelo no tape todo. Si se vuelve a hacer clic en el mismo sitio (a menos de
4 píxeles), se selecciona el **siguiente** de la lista: así se llega a lo que está detrás o
dentro de otra cosa. Un clic en el vacío deselecciona.

### 19.8 Gizmo de mover

- Tres flechas (X rojo, Y verde, Z azul) y una esfera central, dibujadas **sin prueba de
  profundidad** (siempre visibles). Su tamaño es el 16 % de la distancia a la cámara (mínimo
  0.4), así se ven igual de cerca y de lejos.
- **Flecha**: se calcula el punto del eje más cercano al rayo del ratón al empezar y en cada
  fotograma; el objeto se mueve la diferencia a lo largo de ese eje.
- **Esfera**: el objeto se mueve sobre el plano horizontal a su altura, siguiendo el punto donde
  el rayo corta ese plano.
- **Ctrl**: ajusta la posición a múltiplos de 0.25.
- Todo el arrastre es una sola entrada de deshacer.

### 19.9 Assets y arrastrar y soltar

`AssetCatalog` recorre (recursivamente) `models/` (`.obj`), `textures/` (`.png`, `.jpg`,
`.jpeg`, `.bmp`, `.tga`) y `audio/` (`.wav`, `.ogg`, `.mp3`, `.flac`). El panel Assets los
muestra en pestañas (texturas con miniatura). "Volver a leer la carpeta" detecta archivos nuevos.

Al soltar en el visor (en el punto del suelo bajo el ratón, ajustado a 0.25):

| Se suelta | Resultado |
|---|---|
| Modelo | Nueva entidad con ese modelo. Si hay una textura cuyo nombre contiene el del modelo, se le asigna. |
| Textura sobre una entidad | Cambia su `texture` (o su `sprite` si es un sprite). |
| Textura sobre el vacío | Nuevo sprite de 2 de alto con la proporción de la imagen. |
| Sonido | Nuevo emisor, 1 unidad por encima del suelo. |

Doble clic: en una textura, se aplica a la entidad seleccionada; en un modelo o sonido, se añade
en el centro de la vista.

### 19.10 Añadir, duplicar, eliminar

- El menú **Añadir** crea en el centro de la vista (el punto del suelo que se ve en el centro):
  entidad de modelo, sprite, trigger (`[3, 2.4, 3]`, `do: []`), luz (2 por encima del suelo,
  color cálido, radio 8) o sonido posicional.
- Los nombres nuevos son únicos (`casa`, `casa_2`, `casa_3`...).
- **Duplicar** copia el elemento con nombre `<nombre>_copia` (único) y 1 unidad a la derecha en X.
- **Eliminar** borra el elemento. Si su nombre aparece en otro sitio del documento (una acción o
  condición que lo nombra), avisa en Mensajes, pero **no** arregla esas referencias.

### 19.11 Archivo

- **Abrir**: lista los `.json` de `assets/scenes/`. Al abrir se vacía toda la caché de assets y
  se vuelve a leer la carpeta (así se ven los cambios hechos fuera del editor).
- **Nueva escena**: pide un nombre (letras, números, `-` y `_`), y crea `scenes/<nombre>.json`
  con cámara, jugador, colisión automática, límites de ±19 y un suelo de 40x40 con la primera
  textura que contenga "suelo".
- **Guardar** (Ctrl+S): formatea el JSON con `JsonFormat.hpp` (objetos y arreglos de menos de
  100 caracteres y 3 niveles en una línea, el resto expandido con 2 espacios). Comprueba que el
  texto vuelve a dar exactamente el mismo documento; si no, usa `dump(2)`. Escribe con la misma
  escritura atómica que las partidas, dejando la versión anterior en `<escena>.json.bak`.
- **Recargar** (F5): vuelve a leer del disco.
- **Cambios sin guardar**: abrir, crear, recargar o cerrar con cambios pendientes pregunta
  Guardar / Descartar / Cancelar.
- La disposición de los paneles se guarda en `retro_editor.ini` junto al ejecutable. "Ver →
  Restablecer paneles" vuelve a la disposición por defecto (hace falta la primera vez que se
  añade un panel nuevo, como Validacion, para que se ancle abajo).

### 19.12 Editor de lógica (`editor/EditorLogic.cpp`)

Edita en formularios lo descrito en §8–§11, siempre con el protocolo de §19.3: lo que el cargador
real rechace se revierte y se explica en Mensajes, así que el editor no repite las reglas del
formato.

**Listas** (acciones y condiciones): cada elemento es un nodo plegable con **Subir**, **Bajar** y
**Quitar**. Los botones solo apuntan qué hacer; el cambio se aplica al terminar de dibujar la
lista, porque borrar o mover dentro del bucle dejaría colgando las referencias a los elementos
que todavía se están dibujando. Cada operación es un paso de deshacer.

**Acciones** (`do` de triggers e interacciones): "+ Anadir accion" crea una de los 8 tipos con
valores de partida. Cada tipo muestra sus campos:

| Tipo | Campos |
|---|---|
| Mensaje | texto de varias líneas (con `{bandera}`), segundos |
| Fijar / Sumar a bandera | nombre, valor o cantidad |
| Cambiar de escena | escena (desplegable de `scenes/`), punto de llegada opcional (posición y giro) |
| Teletransportar | destino, giro opcional |
| Sonido | archivo, volumen, dónde suena ("en la cabeza", en un punto o en una entidad) y, si tiene posición, radio y distancia mínima |
| Música | archivo, volumen, fundido |
| Parar música | fundido |

Una acción con una clave que no se reconoce se muestra en solo lectura.

**Condiciones** (`if` de triggers e interacciones, `showIf` de entidades): admite los tres
formatos del cargador. Sin clave = "siempre se cumple"; un objeto = una condición; un arreglo =
"deben cumplirse todas". Al añadir una segunda condición a una que estaba sola se convierte en
arreglo, y al quitar la última se borra la clave. Cada nodo tiene un desplegable **Tipo**:

- **Bandera**: nombre, comparación (`==` por defecto, que no se escribe), valor.
- **Todas se cumplen (Y)** / **Alguna se cumple (O)**: lista de hijos. El último hijo no se puede
  quitar (un grupo vacío no es válido); se quita el grupo entero.
- **No se cumple**: un único hijo.

Cambiar el tipo conserva lo que puede: de "todas" a "alguna" mantiene los hijos, una bandera se
envuelve en el grupo o en el "no", un "no" que pasa a grupo se lleva su hijo, y un grupo que pasa
a "no" se queda con el primero. A partir del nivel 8 de anidación solo se ofrecen banderas (el
límite del cargador). El reemplazo del nodo se aplica al final de dibujarlo, cuando ya nadie usa
sus hijos.

**Interacción** (`interact` de una entidad): "Anadir interaccion" la crea con el texto
"Interactuar" y un mensaje (el cargador exige al menos una acción, por eso la última acción no se
puede quitar). Se editan el texto tras "E:", el alcance, "una sola vez", la **caja propia**
(al marcarla empieza con la caja de colisión o, si no hay, la del modelo; se edita como centro y
tamaño; sin ella se usa la caja de colisión o la del modelo), las condiciones y las acciones.

### 19.13 Panel Validacion (`editor/EditorValidation.cpp`)

Busca lo que el cargador **acepta** pero fallará o no tendrá efecto al jugar. No impide guardar:
a mitad de construir una escena es normal tener cosas a medias.

| Gravedad | Qué detecta |
|---|---|
| Error | `goto_scene` a una escena que no existe; `play_sound` en una entidad (`"at": "nombre"`) que no existe. |
| Aviso | `play_sound` o `play_music` con un archivo que no existe; una bandera que se **consulta** (en condiciones o en `{bandera}` de un mensaje) pero que **ninguna escena** cambia, así que siempre vale 0. |
| Info | Un trigger sin acciones; una bandera que se cambia pero que ninguna escena consulta. |

Las banderas se comparan con **todas** las escenas de `assets/scenes/`: la llave puede
recogerse en una escena y usarse en otra. La escena abierta se analiza tal como está en el
editor (también sin guardar); las demás, como están en el disco.

Cada problema dice dónde está ("Trigger 'puerta': ..."). Un clic lo **selecciona y lo enfoca**.
El título muestra el número de errores y avisos.

El análisis se repite solo cuando el documento cambia (un contador `docRevision_` sube en cada
cambio aceptado, al deshacer y al abrir) y **no mientras se arrastra** un valor o el gizmo, para
no leer las demás escenas en cada fotograma. "Volver a comprobar" lo fuerza, por ejemplo después
de añadir un sonido a la carpeta.

---

## 20. Herramientas de contenido

Las de Python requieren `pip install pillow numpy` (`tools/requirements.txt`).

### Modelos en Blender (`tools/blender/`)

Cada modelo se construye con un script de Python que se ejecuta **dentro de Blender** (desde
el MCP de Blender o pegándolo en su consola) y exporta a `assets/models/`:

```python
exec(open(ruta).read(), {"__file__": ruta, "__name__": "__main__"})
```

| Script | Modelos |
|---|---|
| `molino.py` | `molino` y `molino_aspas` (las aspas giran con `"spin": [0, 0, -40]`; van colocadas en el buje: posición del molino + `[0, 6.9, 2.1]`). |
| `aldea_props.py` | `casa_madera`, `casa_piedra` y sus variantes `_noche` con las ventanas encendidas, `pozo`, `valla`, `barril`, `caja`, `carreta`. |
| `cementerio.py` | `capilla`, `cripta`, `tumba`, `lapida`, `cruz`, `verja`, `farol` (luz con offset `[0, 2.75, 0]`), `banco`, `fuente`. |
| `subterraneo.py` | `cripta_interior`, `tuneles`, `sarcofago`, `atril`, `puerta_cripta`, `estatua`, `reja`, `cuerda_pozo`, `escalera_mano`, `trampilla`. |
| `personajes.py` | `granjero`, `aldeana`, `anciano`, `nino`, `hombre`, `mujer`, `sepulturero`, `sereno`, `encapuchado` (el ermitaño de los túneles: capucha de sudadera hasta los hombros y gabardina larga abierta por delante, ambas en la región del pantalón del atlas). |

`retro_comun.py` tiene las piezas comunes:

- `Malla`: cajas, prismas, segmentos entre dos puntos, anillos apilados, polígonos y
  "calcomanías" (puertas y ventanas sobre una pared). Las UV se proyectan en el plano de cada
  cara con la escala del material en metros, así las tablas y tejas quedan derechas y sin
  estirar.
- `material()`: material de Blender con la textura de `assets/textures/` (filtro de punto).
- `exportar()`: `.obj` triangulado con los ejes del motor (frente del modelo en -Y de Blender →
  +Z del motor) y `.mtl` limpio con rutas `../textures/`.

Los personajes usan la distribución del atlas de 128x128 (cara, nuca, pelo, piel, torso,
mangas, antebrazos, pantalón y zapatos): cada parte se mapea a su región, y el hueco `atlas`
se exporta sin textura para que la entidad la elija con `"texture"`. Los complementos
(sombrero, horca, bastón, pala, farol) traen su propio material.

### `tools/gen_texturas.py`

Texturas de materiales (`mat_*.png`): madera, madera gris, piedra, revoque, tejas, paja, lona,
hierro, agua, un negro para huecos, y puerta y ventanas (apagadas y encendidas). Ruido periódico
para que se repitan sin costuras, y la misma cuantización que `texconv.py`. `--preview` guarda
ampliaciones.

### `tools/gen_musica.py`

Compone y sintetiza la música de las escenas (`musica_<escena>_v2.wav`: aldea, noche,
cementerio, demo, cripta y tuneles). Piezas originales lentas y melancólicas, con instrumentos de síntesis
aditiva hechos con numpy: pad de cuerdas, coro con formantes ("a" u "o"), caja de música,
flauta con soplo, piano, campana de iglesia, bajo y dron. Reverb de sala grande y eco de ida y
vuelta.

- **Bucle perfecto**: todo se mezcla en un buffer circular (lo que pasa del final vuelve al
  principio) y la reverb es una convolución circular por FFT, así que la cola de la reverb del
  final suena sobre el principio y el bucle no tiene corte.
- 22050 Hz estéreo de 16 bits, como el resto del audio pero en estéreo.
- Las piezas se escriben compás a compás en el propio script (acordes, bajos, arpegios y
  melodías como listas de `(compás, tiempo, duración, nota MIDI)`): para cambiar una melodía se
  edita la lista y se vuelve a generar.
- Las músicas antiguas (`musica_aldea.wav`...) siguen en `assets/audio/`; las escenas usan las
  `_v2`.

### El subterráneo: `subterraneo.py` y `tuneles_mapa.py`

- **Interior de la cripta**: sala cerrada con bóveda de cañón, nichos y una escalera que baja por
  un hueco del suelo. Como el jugador no puede bajar de Y=0, el **suelo de la sala está a 3 m**
  (tres bloques de colisión alrededor del hueco) y la escalera baja de 3 a 0 hasta un rellano
  oscuro; ahí un trigger lleva a los túneles.
- **Túneles**: el mapa es una cuadrícula de celdas de 3 m en `tuneles_mapa.py` (Python puro):
  salas como rectángulos y túneles como polilíneas de celdas. `python tuneles_mapa.py` lo dibuja
  y comprueba que todo esté conectado. El modelo pone suelo, techo y paredes por celda en una
  rejilla de 1 m (para que la luz por vértice tenga detalle), con escalones de techo entre salas
  altas y túneles bajos, y luego **deforma con ruido** solo los vértices de tierra: así parecen
  excavados, y lo que toca piedra o madera no se mueve, de modo que encaja. Todas las piezas
  comparten los mismos cortes en altura (las alturas de techo), para que no queden grietas.
- La colisión de los túneles son bloques de roca por filas, metidos 0.2 m en el túnel: la pared
  deformada puede entrar hasta ~0.3 m y la cámara no debe atravesarla.
- La cuerda del pozo y la escalera de mano son modelos aparte porque se usan con **E**.

### `tools/gen_sonidos.py`

Efectos y ambientes del subterráneo, mono a 22050 Hz: `cripta_amb` y `tuneles_amb` (bucles
perfectos: buffer circular y la reverb de `gen_musica.py`), `paso_tierra_1..3`, `reja`,
`derrumbe` y `gemido`.

### `tools/gen_sprites.py`

Sprites con transparencia (`spr_*.png`): árbol seco, cuervo, velas, hierba alta, flores y setas
luminosas; y para el subterráneo cristales, telaraña, huesos, raíces que cuelgan, antorcha y unos
ojos rojos. Pixel art procedural con contorno oscuro y alfa duro. Las velas y las setas se usan
con `"emissive": true` y una `"light"` en la entidad.

### `tools/gen_personajes.py`

Pinta atlas de personaje completos (`pj_<nombre>.png`, 128x128, misma distribución que
`charatlas.py`) sin foto: una cara pixel art procedural más el pelo, la nuca, la piel y la ropa.
Cada personaje es una entrada del diccionario `PERSONAJES` con su piel, pelo, ojos, peinado
(`corto`, `raya`, `despeinado`, `largo`, `calvo`, `gorro`), barba o bigote, rasgos (pecas,
rubor, ojeras, arrugas de viejo, cara de niño, gafas, sonrisa) y ropa (color, cuadros o rayas,
tirantes, delantal, solapas, botones, cinturón, falda o pantalón). Usa la misma cuantización
que `texconv.py`. Los personajes de `aldea`, `noche` y `cementerio` usan estos atlas; el de la
demo sigue con `yo_tex.png`.

### `tools/texconv.py`

Convierte cualquier imagen en una textura con aspecto PS1:

```bash
python tools/texconv.py ladrillo.jpg assets/textures/ladrillo.png --size 64 --tileable --noise 0.02
```

Reduce por área a `--size` (por defecto 64x64), ajusta brillo, contraste, saturación y tinte,
añade ruido opcional, con `--tileable` hace que la imagen se repita sin costuras (la mezcla con
su versión desplazada media imagen), cuantiza a `--levels` niveles (32) con *dithering* Bayer
(la misma matriz que los shaders), opcionalmente a una paleta de `--palette` colores, y aplica
alfa duro (`--alpha-cut`). `--preview` guarda una ampliación x8.

### `tools/charatlas.py`

Pone una foto de cara en el atlas de 128x128 de los personajes `persona_m` / `persona_f`
(la cara ocupa 48x56 en la esquina superior izquierda y se proyecta plana sobre la cabeza, como
en los juegos de la época):

```bash
python tools/charatlas.py assets/textures/persona_m1_tex.png foto.jpg assets/textures/luis_tex.png --box 80,40,260,300
```

Recorta la región `--box` (de la coronilla a la barbilla), la reduce, la cuantiza, deduce piel y
pelo de la foto, regenera nuca, lados, cuello, manos y pelo para que casen, y opcionalmente
cambia camisa (`--shirt`) y pantalón (`--pants`). `--guide` guarda la foto con el recuadro y la
línea de los ojos para ajustar el `--box`.

---

## 21. Depuración

### Teclas del juego

| Tecla | Efecto |
|---|---|
| 1 | Temblor de vértices sí / no |
| 2 | 32 / 256 niveles de color |
| 3 | *Dithering* sí / no |
| 4 | Niebla sí / no |
| 6 | Luces puntuales sí / no |
| F1 | Ayuda en pantalla |
| F2 | Cámara orbital |
| F3 | Cajas: colisiones en rojo (incluidos los muros de `bounds`), triggers en verde |
| F5 | Recargar la escena del disco |
| F6 | Borrar banderas y `once` |
| F7 | Modo de escalado |
| F8 | Acabado del post-proceso sí / no |
| M | Silencio |

### Dónde mirar cuando algo falla

- **La consola**: errores de carga de escena con su contexto, archivos de audio que fallan
  (avisados una vez), `goto_scene` a escenas inexistentes, `play_sound` a entidades
  inexistentes, problemas al guardar o cargar.
- **El panel Mensajes** del editor: por qué se descartó un cambio, avisos al borrar, assets que
  no cargan.
- **El panel Validacion** del editor: escenas, sonidos o entidades que una acción nombra y no
  existen, y banderas que se consultan pero nadie cambia (§19.13).
- **Si todo se ve "moderno"** (sin temblor ni trama): probablemente una tecla de depuración
  (1, 2 o 3) está activa. Un shader que falta o no compila no pasa en silencio: el motor no
  arranca y la consola dice cuál.
- **Si un objeto no choca**: F3. ¿Tiene `collider`? ¿La escena tiene `autoCollide`? ¿Es un plano
  o un sprite (no tienen colisión automática)? ¿Está visible?
- **Si un trigger no se dispara**: ¿apareciste dentro (el primer fotograma no cuenta)? ¿es
  `once` y ya se usó (F6)? ¿se cumplen sus condiciones? ¿estás en cámara orbital?
- **Si un cambio en el editor no aparece en el juego**: el juego lee la copia de
  `build/bin/assets/`; hay que volver a compilar.
