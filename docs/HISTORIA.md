# La historia (capítulo 1: el bosque)

El guion del juego tal como está montado en las escenas: qué se cuenta, dónde y con qué banderas.
Para escribir lo que viene después sin contradecirlo.

## La trama

La aldea vive de un **pacto** antiguo. El molino gira aunque no haya viento, las cosechas no
fallan... y a cambio, una vez por generación, alguien es entregado al bosque del este, siempre
por el mismo sendero marcado con figuras de ramas, hasta un árbol hueco. El pacto se hizo en
**Valdemora** (1702), el primer pueblo, que hoy está en ruinas dentro del bosque: *"El viento es
un regalo. El regalo se paga."*

La última entregada fue **Clara Rosales**, de 7 años, desaparecida junto al lago. Una partida
de búsqueda de seis personas fue a por ella contra la voluntad del alcalde ("el bosque ya había
elegido") y no volvió.

El **forastero** (el jugador) es el siguiente pago. Por eso todos le van empujando hacia el
este sin decírselo: el sepulturero le da la llave ("Otra alma perdida..."), el pescador le habla
del bosque negro... Al final, los aldeanos le esperan en el árbol con faroles. Él no muere: el
bosque lo devuelve, y eso **no había pasado nunca**. El molino se para.

## Las pistas antes del bosque

| Dónde | Qué | Qué significa |
|---|---|---|
| Aldea | El molino gira sin viento (el niño, el guardia) | El regalo del pacto |
| Aldea | El niño: "vi unos ojos rojos en el fondo del pozo" | Lo que hay bajo la aldea |
| Lago | La barca: agua dentro y un zapato pequeño | El zapato de Clara |
| Lago | El columpio: se mueve sin viento, la cuerda está tibia | Clara jugaba ahí |
| Lago | El pescador: "las mejores tardes son las últimas"; no vayas al este | Sabe lo que pasa |
| Lago | La música (nana en mi mayor) | Volverá rota en el bosque |
| Cementerio | El sepulturero da la llave; luego "yo no me he movido de aquí" | Hay algo con su forma |
| Capilla | El órgano despierta "algo, muy abajo" | La cripta y los túneles |

## Las escenas del bosque

1. **Bosque negro** (`bosque_negro.json`): el cartel "no mires atrás", la figura alta al fondo,
   las figuras de ramas, los pasos detrás (la figura está a tu espalda: `seen`), la cabaña donde
   alguien responde, el círculo de piedras con un corazón tibio. Un árbol caído corta el sendero;
   por debajo se pasa al campamento.
2. **El campamento** (`campamento.json`): el cartel de SE BUSCA de Clara, tachado ("YA ESTÁ
   PAGADA"); el diario de la partida en tres páginas (tienda, fogata, tienda hundida); la radio
   ("no es el bosque... es el pueblo... nos mandaron..."); una tienda que se mueve hasta que
   apartas la vista y luego tiene el saco tibio; seis pares de botas en fila, las huellas
   descalzas hacia el norte; la chaqueta de Ruiz en una figura de ramas. La radio habla de golpe
   cuando ya está a tu espalda.
3. **La aldea vieja** (`aldea_vieja.json`): Valdemora. Cada puerta con una X roja (cada familia
   que pagó); una cuna con un molinillo que gira sin aire; el pozo tapado, arañado por debajo; la
   placa del pacto; el cementerio de niños (una cruz por generación desde 1703); la cruz de Clara
   con velas y flores frescas y alguien tarareando la nana; una fosa recién cavada "de tu tamaño"
   con una cruz sin nombre; el sepulturero cavando a lo lejos, que desaparece al dejar de mirarlo.
4. **El árbol hueco** (`arbol_hueco.json`): zapatos de niño entre las raíces, campanillas, la nana
   en una caja de música rota y nombres tallados dentro del tronco: el último, recién hecho, es el
   tuyo. Entonces la figura alta está dentro del hueco, y por el sendero vienen los aldeanos
   (el anciano, la aldeana, el granjero, el sereno y el sepulturero). Cada vez que dejas de
   mirarlos están más cerca; al final el anciano, a tres pasos: *"Lo siento, forastero. El molino
   tiene que seguir girando. Y el viento... alguien tiene que pagarlo."*
5. **Epílogo** (aldea): despiertas junto al pozo con tierra bajo las uñas. El molino se ha
   parado. Los aldeanos (y el pescador, el sereno, el sepulturero) tienen algo nuevo que decir.

## Banderas

| Bandera | La pone | La usa |
|---|---|---|
| `bn_lejos`, `bn_pasos`, `bn_detras_fin` | bosque negro (triggers, `seen`) | la figura al fondo y la de detrás |
| `cm_quieta` | campamento (`seen` de la tienda) | la tienda deja de moverse |
| `av_sepulturero_fin` | aldea vieja (`seen`) | el sepulturero desaparece |
| `ah_nombre` | árbol hueco (leer la inscripción) | la figura en el hueco y la 1ª fila de aldeanos |
| `ah_e2`, `ah_e3` | árbol hueco (`seen` del anciano) | las filas más cercanas |
| `capitulo1_fin` | diálogo `anciano_final` | despertar, molino parado, diálogos nuevos |

## Para el capítulo siguiente

Quedan abiertos: qué es la figura alta y qué hay bajo la cripta (los ojos rojos de los túneles, lo
que oye el órgano); quién llama desde dentro de la cabaña; quién es el sepulturero que no se movió;
y el forastero, que vuelve vivo de donde nadie vuelve: el pacto está roto, o ha cambiado de precio.
