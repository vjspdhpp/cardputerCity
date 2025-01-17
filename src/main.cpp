
#include "M5Cardputer.h"
#include <TFT_eSPI.h>
#include <stdio.h>
#include <time.h>

#define USE_DMA_TO_TFT

#define COLOR_DEPTH 16

#define DELAY 1000

TFT_eSPI tft = TFT_eSPI(); // Declare object "tft"

TFT_eSprite spr = TFT_eSprite(&tft);

#define S3L_FLAT 0
#define S3L_NEAR_CROSS_STRATEGY 3
#define S3L_PERSPECTIVE_CORRECTION 2
#define S3L_SORT 0
#define S3L_STENCIL_BUFFER 0
#define S3L_Z_BUFFER 2

#define S3L_PIXEL_FUNCTION drawPixel

#define S3L_RESOLUTION_X 240
#define S3L_RESOLUTION_Y 135

#include "small3dlib.h"

#include "carModel.h"
#include "cityModel.h"
#include "cityTexture.h"

#define TEXTURE_W 256
#define TEXTURE_H 256

#define MAX_VELOCITY 1000
#define ACCELERATION 700
#define TURN_SPEED 300
#define FRICTION 600

S3L_Model3D models[2];

const uint8_t collisionMap[8 * 10] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1,
    0, 1, 0, 1, 2, 2, 1, 0, 0, 0, 0, 3, 1, 2, 1, 0, 1, 1, 3, 1,
    2, 0, 0, 0, 1, 1, 3, 3, 1, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

S3L_Scene scene;

uint32_t frame = 0;

void clearScreenBlue() {
  // uint32_t index = 0;

  // for (uint16_t y = 0; y < S3L_RESOLUTION_Y; ++y) {
  //   S3L_Unit t = S3L_min(S3L_F, ((y * S3L_F) / S3L_RESOLUTION_Y) * 4);

  //   uint32_t r = S3L_interpolateByUnit(200, 242, t);
  //   uint32_t g = S3L_interpolateByUnit(102, 255, t);
  //   uint32_t b = S3L_interpolateByUnit(255, 230, t);

  //   // convert r,g,b to 16 bit color
  //   uint32_t color16 = M5Cardputer.Display.color565(r, g, b);
  //   for (uint16_t x = 0; x < S3L_RESOLUTION_X; ++x) {
  //     M5Cardputer.Display.drawPixel(x, y, color16);
  //     index++;
  //   }
  // }
  // M5Cardputer.Display.clear();
  spr.fillScreen(TFT_BLUE);
}

uint32_t previousTriangle = -1;
S3L_Vec4 uv0, uv1, uv2;

void sampleTexture(const uint8_t *tex, int32_t u, int32_t v, uint8_t *r,
                   uint8_t *g, uint8_t *b) {
  u = S3L_wrap(u, TEXTURE_W);
  v = S3L_wrap(v, TEXTURE_H);

  const uint8_t *t = tex + (v * TEXTURE_W + u) * 4;

  *r = *t;
  t++;
  *g = *t;
  t++;
  *b = *t;
}

void drawPixel(S3L_PixelInfo *p) {
  if (p->triangleID != previousTriangle) {
    const S3L_Index *uvIndices;
    const S3L_Unit *uvs;

    if (p->modelIndex == 0) {
      uvIndices = cityUVIndices;
      uvs = cityUVs;
    } else {
      uvIndices = carUVIndices;
      uvs = carUVs;
    }

    S3L_getIndexedTriangleValues(p->triangleIndex, uvIndices, uvs, 2, &uv0,
                                 &uv1, &uv2);

    previousTriangle = p->triangleID;
  }

  uint8_t r, g, b;

  S3L_Unit uv[2];

  uv[0] = S3L_interpolateBarycentric(uv0.x, uv1.x, uv2.x, p->barycentric);
  uv[1] = S3L_interpolateBarycentric(uv0.y, uv1.y, uv2.y, p->barycentric);

  sampleTexture(cityTexture, uv[0] >> 1, uv[1] >> 1, &r, &g, &b);
  uint32_t color16 = M5Cardputer.Display.color565(r, g, b);
  spr.drawPixel(p->x, p->y, color16);
}

void draw() {
  S3L_newFrame();
  clearScreenBlue();
  S3L_drawScene(scene);
}

static inline uint8_t collision(S3L_Vec4 worldPosition) {
  worldPosition.x /= S3L_F;
  worldPosition.z /= -S3L_F;

  uint16_t index = worldPosition.z * 8 + worldPosition.x;

  return collisionMap[index];
}

static inline void handleCollision(S3L_Vec4 *pos, S3L_Vec4 previousPos) {
  S3L_Vec4 newPos = *pos;
  newPos.x = previousPos.x;

  if (collision(newPos)) {
    newPos = *pos;
    newPos.z = previousPos.z;

    if (collision(newPos))
      newPos = previousPos;
  }

  *pos = newPos;
}

int16_t fps = 0;
int16_t fps2 = 0;
int setupSmall3d(void) {

  cityModelInit();
  carModelInit();

  models[0] = cityModel;
  models[1] = carModel;

  S3L_sceneInit(models, 2, &scene);

  S3L_transform3DSet(1800, 16, -3317, 0, -510, 0, 512, 512, 512,
                     &(models[1].transform));

  int running = 1;

  clock_t nextPrintT;

  nextPrintT = clock();

  S3L_Vec4 carDirection;

  S3L_vec4Init(&carDirection);

  scene.camera.transform.translation.y = S3L_F / 2;
  scene.camera.transform.rotation.x = -S3L_F / 16;

  int16_t velocity = 0;

  while (1) // main loop
  {
    clock_t frameStartT = clock();

    models[1].transform.rotation.y +=
        models[1].transform.rotation.z; // overturn the car for the rendering
    
    // Serial.println("update");
    draw();

    models[1].transform.rotation.y -=
        models[1].transform.rotation.z; // turn the car back for the physics

    fps++;

    clock_t nowT = clock();

    double timeDiff = ((double)(nowT - nextPrintT)) / CLOCKS_PER_SEC;
    double frameDiff = ((double)(nowT - frameStartT)) / CLOCKS_PER_SEC;
    int16_t frameDiffMs = frameDiff * 1000;
    // Serial.printf("timeDiff: %f\n", timeDiff);
    if (timeDiff >= 1.0) {
      nextPrintT = nowT;
      // Serial.printf("FPS: %d\n", fps);
      fps2 = fps;
      fps = 0;
    }
    spr.drawString("FPS: " + String(fps2), 10, 0);

    int16_t step = (velocity * frameDiffMs) / 1000;
    int16_t stepFriction = (FRICTION * frameDiffMs) / 1000;
    int16_t stepRotation = TURN_SPEED * frameDiffMs *
                           S3L_max(0, velocity - 200) / (MAX_VELOCITY * 1000);

    int16_t stepVelocity = S3L_nonZero((ACCELERATION * frameDiffMs) / 1000);

    if (stepRotation == 0 && S3L_abs(velocity) >= 200)
      stepRotation = 1;

    if (velocity < 0)
      stepRotation *= -1;
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE))
      running = 0;
    else if (M5Cardputer.Keyboard.isKeyPressed(',')) {
      models[1].transform.rotation.y += stepRotation;
      models[1].transform.rotation.z =
          S3L_min(S3L_abs(velocity) / 64, models[1].transform.rotation.z + 1);
    } else if (M5Cardputer.Keyboard.isKeyPressed('/')) {
      models[1].transform.rotation.y -= stepRotation;
      models[1].transform.rotation.z =
          S3L_max(-S3L_abs(velocity) / 64, models[1].transform.rotation.z - 1);
    } else
      models[1].transform.rotation.z = (models[1].transform.rotation.z * 3) / 4;

    S3L_rotationToDirections(models[1].transform.rotation, S3L_F, &carDirection,
                             0, 0);

    S3L_Vec4 previousCarPos = models[1].transform.translation;

    int16_t friction = 0;

    if (M5Cardputer.Keyboard.isKeyPressed('2'))
      velocity =
          S3L_min(MAX_VELOCITY, velocity + (velocity < 0 ? (2 * stepVelocity)
                                                         : stepVelocity));
    else if (M5Cardputer.Keyboard.isKeyPressed('w'))
      velocity =
          S3L_max(-MAX_VELOCITY, velocity - (velocity > 0 ? (2 * stepVelocity)
                                                          : stepVelocity));
    else
      friction = 1;

    models[1].transform.translation.x += (carDirection.x * step) / S3L_F;
    models[1].transform.translation.z += (carDirection.z * step) / S3L_F;

    uint8_t coll = collision(models[1].transform.translation);

    if (coll != 0) {
      if (coll == 1) {
        handleCollision(&(models[1].transform.translation), previousCarPos);
        friction = 2;
      } else if (coll == 2) {
        // teleport the car
        models[1].transform.translation.x += 5 * S3L_F;
        models[1].transform.translation.z += 2 * S3L_F;
      } else {
        // teleport the car
        models[1].transform.translation.x -= 5 * S3L_F;
        models[1].transform.translation.z -= 2 * S3L_F;
      }
    }

    if (velocity > 0)
      velocity = S3L_max(0, velocity - stepFriction * friction);
    else
      velocity = S3L_min(0, velocity + stepFriction * friction);

    S3L_Unit cameraDistance = S3L_interpolate(S3L_F / 2, (3 * S3L_F) / 4,
                                              S3L_abs(velocity), MAX_VELOCITY);

    scene.camera.transform.translation.x =
        scene.models[1].transform.translation.x -
        (carDirection.x * cameraDistance) / S3L_F;

    scene.camera.transform.translation.z =
        scene.models[1].transform.translation.z -
        (carDirection.z * cameraDistance) / S3L_F;

    scene.camera.transform.rotation.y = models[1].transform.rotation.y;

    // sdlUpdate();
    spr.pushSprite(0, 0);

    frame++;
  }

  return 0;
}

void draw_function(LovyanGFX *gfx) {
  int x = rand() % gfx->width();
  int y = rand() % gfx->height();
  int r = (gfx->width() >> 4) + 2;
  uint16_t c = rand();
  gfx->fillRect(x - r, y - r, r * 2, r * 2, c);
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  delay(1000);
  Serial.begin(115200);
  Serial.println();

  // Initialise the TFT registers
  tft.init();
  tft.setRotation(1);

  // Optionally set colour depth to 8 or 16 bits, default is 16 if not specified
  // spr.setColorDepth(8);

  // Create a sprite of defined size
  spr.createSprite(240, 135);

  // Clear the TFT screen to blue
  tft.fillScreen(TFT_BLUE);

  setupSmall3d();
}

void loop() { draw(); }
