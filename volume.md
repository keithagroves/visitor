
On Shaping Light
Jun 10, 2025Jun 10, 2025
As I became more familiar with post-processing over the past few months, I was curious to push those newly learned techniques beyond pure stylization to achieve something more functional. I wanted to find new ways to enrich my 3D work which wouldn't be possible without leveraging effects and passes alongside custom shaders.

As it turns out, post-processing is great entrypoint to enhance a 3D scene with atmospheric and lighting effects, allowing for more realistic and dramatic visuals. Because these effects operate in screen space, their performance cost is decoupled from the underlying scene's complexity, making them an efficient solution for balancing performance and visual quality. At the end of the day, when we work with effects, we are still just drawing pixels on a screen.

Among those effects, Volumetric Lighting was the first one to catch my attention as I always wanted to create those beautiful beams of light, shining through trees or buildings, creating a dreamy and atmospheric vibe to my work (heavily featured in "Clair Obscur: Expedition 33" which I've been playing a lot recently for "research purposes"). On top of that, I found out that these light effects can be made possible with Volumetric Raymarching, a topic I've covered in the past but haven't found any practical application in my React Three Fiber work so far!

Thus, by combining these two seemingly unrelated pieces, that are post-processing and raymarching, in the context of volumetric lighting, I discovered a new set of tricks to complement my shader work, enhance the visual of my scenes, and, more importantly, share with you. In this article, I'll showcase not only what makes a good volumetric lighting effect using raymarching, but also detail every technique behind it, such as coordinate space transformations, shadow mapping, and new usage of depth buffers, as well as further expansions into multi-directional lighting and multi-light examples.

The scenes of the demos featured throughout this article are inspired by the work of the following talented folks:

hmans.dev for his space scene which inspired one of my demos and from which I borrowed the asteroid asset.
jackdavenport who made a beautiful scene applying volumetric lighting on ShaderToy.
Vlad Ponomarenko whose Spline work helped me craft a great demo to apply volumetric lighting and make this effect shine.
Enjoying my writing and feeling like supporting my work?
You can show your appreciation by buying me a coffee (I really really really do like coffee) which will give me the much-needed energy (and fuel my caffeine addiction) to take on more ambitious/high-quality articles and (probably over-engineered but fun) projects.
As a token of gratitude, your name will be featured on this little screen below!
JOHN SIMONS 3 COFFEES, BARDUR 1 COFFEE, @CODEYCURLEY 3 COFFEES, BOBBY ROW 10 COFFEES, BY HUY 20 COFFEES, GRANT 1 COFFEE, SUJAN KHADGI 20 COFFEES, @MARVINKENNIS 10 COFFEES, DARRIN MASSENA 10 COFFEES, SKY DAVIS 1 COFFEE, FLOGUO 5 COFFEES, SA 1 COFFEE, AXEL MÖLLERBERG 1 COFFEE, TOBIAS 1 COFFEE, ARTUR 5 COFFEES, @KENNYPIRMAN 1 COFFEE, ARZAFRAN 3 COFFEES, @MARIJANAPAV 1 COFFEE, MOMO PICHAT 1 COFFEE, DANIEL DESTEFANIS 1 COFFEE, @STUDIOMING.CO 1 COFFEE, GREG CHRISTMAN 3 COFFEES, YM 5 COFFEES, ROMANJEANELIE 1 COFFEE, BEN MCCORMICK (PHOBON) 5 COFFEES, MICHAEL SIKORSKY 10 COFFEES, ANDREW WOAN 5 COFFEES, LIAM PULSIFER 3 COFFEES, @ALEX34230902 5 COFFEES, STEPHEN HANEY 5 COFFEES, ZEH FERNANDES 3 COFFEES, @FFMFEDD 10 COFFEES, DAVE 5 COFFEES, @JOEVINGRACIEN 3 COFFEES, PIXELBEAT 1 COFFEE, @KENNYPIRMAN 4 COFFEES, BEN 3 COFFEES, ANDERSON MANCINI 3 COFFEES, @OTFNK 1 COFFEE, JOSE 1 COFFEE, MARTIN 2 COFFEES, GRAEME 1 COFFEE, @BEN.ORLOFF 1 COFFEE, @DGHEZ_ 1 COFFEE, ANDREW B 1 COFFEE, @HRH_858 3 COFFEES, @JAD_AE 1 COFFEE, @FFDEAD 3 COFFEES, JOSE 1 COFFEE,
JOHN SIMONS 3 COFFEES, BARDUR 1 COFFEE, @CODEYCURLEY 3 COFFEES, BOBBY ROW 10 COFFEES, BY HUY 20 COFFEES, GRANT 1 COFFEE, SUJAN KHADGI 20 COFFEES, @MARVINKENNIS 10 COFFEES, DARRIN MASSENA 10 COFFEES, SKY DAVIS 1 COFFEE, FLOGUO 5 COFFEES, SA 1 COFFEE, AXEL MÖLLERBERG 1 COFFEE, TOBIAS 1 COFFEE, ARTUR 5 COFFEES, @KENNYPIRMAN 1 COFFEE, ARZAFRAN 3 COFFEES, @MARIJANAPAV 1 COFFEE, MOMO PICHAT 1 COFFEE, DANIEL DESTEFANIS 1 COFFEE, @STUDIOMING.CO 1 COFFEE, GREG CHRISTMAN 3 COFFEES, YM 5 COFFEES, ROMANJEANELIE 1 COFFEE, BEN MCCORMICK (PHOBON) 5 COFFEES, MICHAEL SIKORSKY 10 COFFEES, ANDREW WOAN 5 COFFEES, LIAM PULSIFER 3 COFFEES, @ALEX34230902 5 COFFEES, STEPHEN HANEY 5 COFFEES, ZEH FERNANDES 3 COFFEES, @FFMFEDD 10 COFFEES, DAVE 5 COFFEES, @JOEVINGRACIEN 3 COFFEES, PIXELBEAT 1 COFFEE, @KENNYPIRMAN 4 COFFEES, BEN 3 COFFEES, ANDERSON MANCINI 3 COFFEES, @OTFNK 1 COFFEE, JOSE 1 COFFEE, MARTIN 2 COFFEES, GRAEME 1 COFFEE, @BEN.ORLOFF 1 COFFEE, @DGHEZ_ 1 COFFEE, ANDREW B 1 COFFEE, @HRH_858 3 COFFEES, @JAD_AE 1 COFFEE, @FFDEAD 3 COFFEES, JOSE 1 COFFEE,
JOHN SIMONS 3 COFFEES, BARDUR 1 COFFEE, @CODEYCURLEY 3 COFFEES, BOBBY ROW 10 COFFEES, BY HUY 20 COFFEES, GRANT 1 COFFEE, SUJAN KHADGI 20 COFFEES, @MARVINKENNIS 10 COFFEES, DARRIN MASSENA 10 COFFEES, SKY DAVIS 1 COFFEE, FLOGUO 5 COFFEES, SA 1 COFFEE, AXEL MÖLLERBERG 1 COFFEE, TOBIAS 1 COFFEE, ARTUR 5 COFFEES, @KENNYPIRMAN 1 COFFEE, ARZAFRAN 3 COFFEES, @MARIJANAPAV 1 COFFEE, MOMO PICHAT 1 COFFEE, DANIEL DESTEFANIS 1 COFFEE, @STUDIOMING.CO 1 COFFEE, GREG CHRISTMAN 3 COFFEES, YM 5 COFFEES, ROMANJEANELIE 1 COFFEE, BEN MCCORMICK (PHOBON) 5 COFFEES, MICHAEL SIKORSKY 10 COFFEES, ANDREW WOAN 5 COFFEES, LIAM PULSIFER 3 COFFEES, @ALEX34230902 5 COFFEES, STEPHEN HANEY 5 COFFEES, ZEH FERNANDES 3 COFFEES, @FFMFEDD 10 COFFEES, DAVE 5 COFFEES, @JOEVINGRACIEN 3 COFFEES, PIXELBEAT 1 COFFEE, @KENNYPIRMAN 4 COFFEES, BEN 3 COFFEES, ANDERSON MANCINI 3 COFFEES, @OTFNK 1 COFFEE, JOSE 1 COFFEE, MARTIN 2 COFFEES, GRAEME 1 COFFEE, @BEN.ORLOFF 1 COFFEE, @DGHEZ_ 1 COFFEE, ANDREW B 1 COFFEE, @HRH_858 3 COFFEES, @JAD_AE 1 COFFEE, @FFDEAD 3 COFFEES, JOSE 1 COFFEE,
JOHN SIMONS 3 COFFEES, BARDUR 1 COFFEE, @CODEYCURLEY 3 COFFEES, BOBBY ROW 10 COFFEES, BY HUY 20 COFFEES, GRANT 1 COFFEE, SUJAN KHADGI 20 COFFEES, @MARVINKENNIS 10 COFFEES, DARRIN MASSENA 10 COFFEES, SKY DAVIS 1 COFFEE, FLOGUO 5 COFFEES, SA 1 COFFEE, AXEL MÖLLERBERG 1 COFFEE, TOBIAS 1 COFFEE, ARTUR 5 COFFEES, @KENNYPIRMAN 1 COFFEE, ARZAFRAN 3 COFFEES, @MARIJANAPAV 1 COFFEE, MOMO PICHAT 1 COFFEE, DANIEL DESTEFANIS 1 COFFEE, @STUDIOMING.CO 1 COFFEE, GREG CHRISTMAN 3 COFFEES, YM 5 COFFEES, ROMANJEANELIE 1 COFFEE, BEN MCCORMICK (PHOBON) 5 COFFEES, MICHAEL SIKORSKY 10 COFFEES, ANDREW WOAN 5 COFFEES, LIAM PULSIFER 3 COFFEES, @ALEX34230902 5 COFFEES, STEPHEN HANEY 5 COFFEES, ZEH FERNANDES 3 COFFEES, @FFMFEDD 10 COFFEES, DAVE 5 COFFEES, @JOEVINGRACIEN 3 COFFEES, PIXELBEAT 1 COFFEE, @KENNYPIRMAN 4 COFFEES, BEN 3 COFFEES, ANDERSON MANCINI 3 COFFEES, @OTFNK 1 COFFEE, JOSE 1 COFFEE, MARTIN 2 COFFEES, GRAEME 1 COFFEE, @BEN.ORLOFF 1 COFFEE, @DGHEZ_ 1 COFFEE, ANDREW B 1 COFFEE, @HRH_858 3 COFFEES, @JAD_AE 1 COFFEE, @FFDEAD 3 COFFEES, JOSE 1 COFFEE,
Thank you for reading!
Raymarching Light with Post-Processing
As someone whose only experience with post-processing was for stylization purposes, leveraging it alongside volumetric raymarching, which I studied in depth separately, was enticing. The resulting combination would allow one to paint arbitrary light effects onto a scene based on the camera's position, the source of the light, and all of that while taking into account the many objects that could compose the scene.

However, there was still a clear divide that made the result feel unreachable at first: raymarching operates in a three-dimensional space, while post-processing lives in screen space, which is two-dimensional. Thus, before diving into anything related to volumetric lighting proper, we should first learn about the process allowing us to jump from one to the other: coordinate system transformations.

Coordinate systems and transformations
3D scenes operate across several coordinate systems that each have a specific role:

Object/Model space: where coordinates are relative to the object's origin.
World space: the shared coordinate system between all objects on the scene.
View space: where coordinates are relative to the camera. The camera is at (0, 0, 0) looking down the z-axis by default.
Clip space: where coordinates are still related to the camera but transformed to perform "clipping".
Clipping refers to discarding parts of our geometries/scenes that fall outside the camera's view frustum. It ensures our renderer only renders what's visible on the screen.

NDC - Normalized Device Coordinates: the normalized version of the clip space coordinate system 
1
.
Screen-space: the final 2D coordinate system of the rendered image: the frame buffer.
To help you visualize them, the diagram below illustrates each coordinate system defined above.

Diagram illustrating the different coordinate systems and the matrix necessary to jump from one to the next
Diagram illustrating the different coordinate systems and the matrix necessary to jump from one to the next
Given that our volumetric lighting work will start in screen space, due to relying on post-processing, we'll need to reconstruct 3D rays from our camera through each pixel of our effect by converting screen space coordinates into world space coordinates. We can achieve this with the following formulas:

xNDC = uv.x * 2.0 - 1.0

yNDC = uv.y * 2.0 - 1.0

zNDC = depth * 2.0 - 1.0

clipSpace = { x: xNDC, y: yNDC, z: zNDC, 1.0 }

worldSpace = viewMatrixInverse * projectionMatrixInverse * clipSpace

worldSpace /= worldSpace.w

where uv is the UV coordinate of the current fragment of our volumetric lighting post-processing pass and depth, is the depth texture of the underlying scene sampled at that same UV.

From it, we can derive the GLSL function that we will use in later examples:

getWorldPosition function

vec3 getWorldPosition(vec2 uv, float depth) {
  float clipZ = depth * 2.0 - 1.0;
  vec2 ndc = uv * 2.0 - 1.0;
  vec4 clip = vec4(ndc, clipZ, 1.0);

  vec4 view = projectionMatrixInverse * clip;
  vec4 world = viewMatrixInverse * view;

  return world.xyz / world.w;
}
It is important, when implementing the process detailed above, to keep the matrix order as specified. Matrix multiplication is not commutative, i.e., for two matrix A and B, A*B != B*A, the order matters.

Our first light ray
Now that we know the concept of coordinate systems and how to jump from screen space (post-processing pass) to world space (3D space), we can start working towards drawing our first raymarched light. To do so, we can start putting together a simple scene with a VolumetricLighting effect that can take the following arguments:

cameraFar: the depth limit beyond which nothing will be visible or rendered.
projectionMatrixInverse: the camera.projectionMatrixInverse property that we'll use to convert our coordinates from clip-space to view-space.
viewMatrixInverse, which we will set as the camera.matrixWorld property, the matrix that transforms coordinates from view space to world space.
cameraPosition: the position of our camera from which our raymarching will start.
lightDirection: a normalized vector representing the direction the light points toward.
lightPosition: a vector 3 representing the position of our light.
coneAngle, which represents how wide our spotlight aperture is.
These properties are all we need to render a simple volumetric raymarched light shaped like a cone, originating from a given point and pointing in an arbitrary direction.

This article will only cover the case of a PerspectiveCamera. Thus, the transformations used here will not apply to their orthographic counterparts. I may revisit this in the future.

Another essential property from our scene that we will need to make this setup work is the depth buffer. You may have noticed it mentioned in the part on the coordinate system, but it is missing from the set of properties above. That is because we luckily get it for free through postprocessing's Effect class by passing the EffectAttribute.DEPTH in the effect's constructor:

Volumetric Lighting effect class

class VolumetricLightingEffectImpl extends Effect {
  constructor(
    cameraFar = 500,
    projectionMatrixInverse = new THREE.Matrix4(),
    viewMatrixInverse = new THREE.Matrix4(),
    cameraPosition = new THREE.Vector3(),
    lightDirection = new THREE.Vector3(),
    lightPosition = new THREE.Vector3(),
    coneAngle = 40.0
  ) {
    const uniforms = new Map([
      ['cameraFar', new THREE.Uniform(cameraFar)],
      ['projectionMatrixInverse', new THREE.Uniform(projectionMatrixInverse)],
      ['viewMatrixInverse', new THREE.Uniform(viewMatrixInverse)],
      ['cameraPosition', new THREE.Uniform(cameraPosition)],
      ['lightDirection', new THREE.Uniform(lightDirection)],
      ['lightPosition', new THREE.Uniform(lightPosition)],
      ['coneAngle', new THREE.Uniform(coneAngle)],
    ]);

    super('VolumetricLightingEffect', fragmentShader, {
      attributes: EffectAttribute.DEPTH,
      uniforms,
    });

    this.uniforms = uniforms;
  }

  update(_renderer, _inputBuffer, _deltaTime) {
    this.uniforms.get('projectionMatrixInverse').value =
      this.projectionMatrixInverse;
    this.uniforms.get('viewMatrixInverse').value = this.viewMatrixInverse;
    this.uniforms.get('cameraPosition').value = this.cameraPosition;
    this.uniforms.get('cameraFar').value = this.cameraFar;
    this.uniforms.get('lightDirection').value = this.lightDirection;
    this.uniforms.get('lightPosition').value = this.lightPosition;
    this.uniforms.get('coneAngle').value = this.coneAngle;
  }
}
That exposes the depth texture via a built-in uniform depthBuffer in our fragment shader code 
2

With that out of the way, we can start putting together our raymarched light by proceeding as follows:

We sample the depth buffer at a given uv in screen space since those UVs represent the coordinates of our effect.
We reconstruct the 3D position in world space for that pixel using the getWorldPosition function we defined in the first part.
void mainImage(const in vec4 inputColor, const in vec2 uv, out vec4 outputColor) {
  float depth = readDepth(depthBuffer, uv);
  vec3 worldPosition = getWorldPosition(uv, depth);

  //...
}
We set the rayOrigin to our camera's position.
We define the direction of that ray as a vector pointing from the camera toward the current pixel in world space.
We also set our lightPosition (in world space) and lightDirection vectors.
vec3 rayOrigin = cameraPosition;
vec3 rayDir = normalize(worldPosition - rayOrigin);

vec3 lightPos = lightPosition;
vec3 lightDir = normalize(lightDirection);
We then raymarch our light using a classic volumetric raymarching loop that accumulates density/light as we march alongside our ray. We also make sure to attenuate the light as the distance between the source of the light and the current raymarched position increases.
float coneAngleRad = radians(coneAngle);
float halfConeAngleRad = coneAngleRad * 0.5;

float fogAmount = 0.0;
float lightIntensity = 1.0;
float t = STEP_SIZE;

for (int i = 0; i < NUM_STEPS; i++) {
  vec3 samplePos = rayOrigin + rayDir * t;

  if (t > cameraFar) {
    break;
  }

  vec3 toSample = normalize(samplePos - lightPos);
  float cosAngle = dot(toSample, lightDir);

  if (cosAngle < cos(halfConeAngleRad)) {
    t += STEP_SIZE;
    continue;
  }

  float distanceToLight = length(samplePos - lightPos);
  float attenuation = exp(-0.05 * distanceToLight); // could be 1.0 / distanceToLight

  fogAmount += attenuation * lightIntensity;

  t += STEP_SIZE;
}
We finally combine the obtained accumulation of light with the inputColor of our effect and return it as the color output of our fragment shader.
Everything we detailed so far is present in the demo below, which sets up the foundation for the examples we'll see later in this article. In it you'll find:

A simple React Three Fiber scene.
Our VolumetricLightingEffect effect and its definition.
Our effect's fragment shader.
We now have a simple ray of light, built using volumetric raymarching, overlayed on top of a pre-existing scene using post-processing. You can try to move the light position (world space) and see the proper set of pixels drawn in screen space.

Depth-based stopping
When raymarching, we draw our light onto our scene without any constraints. The ray continues marching beyond what the camera/the viewer can see. We thus end up with light or other atmospheric effects such as fog visible through walls or objects, which not only breaks the realism of our effect but also wastes performance because we run our intensive raymarching process beyond where it should have stopped in the first place.

You can visualize this issue in the demo above if you move the camera so the tip of the cone falls behind the sphere. You will see a dot of light through the sphere, which should not be there.

By leveraging our depth texture, which we sampled in screen space, and reconstructing the point in world space, we can calculate the distance between our camera and the current fragment point in world space to stop our raymarching earlier whenever we start sampling beyond the scene's depth:

Diagram showcasing the impact of using depth based stopping when drawing our light using raymarching
Diagram showcasing the impact of using depth based stopping when drawing our light using raymarching
Implementing depth-based stopping in our raymarching loop

void mainImage(const in vec4 inputColor, const in vec2 uv, out vec4 outputColor) {
  float depth = readDepth(depthBuffer, uv);
  vec3 worldPosition = getWorldPosition(uv, depth);

  float sceneDepth = length(worldPosition - cameraPosition);

  //...

  for (int i = 0; i < NUM_STEPS; i++) {
    vec3 samplePos = rayOrigin + rayDir * t;

    if (t > sceneDepth || t > cameraFar) {
      break;
    }
    // ...
  }
  // ...
}
Adding it to our previous demo fixes the issue where the light source could be seen through the sphere when moving the camera around in the scene.

Shaping our light ray
We now have a robust first pass at volumetric lighting. While we did give it somewhat of a shape from the get-go in the first example, I still wanted to dedicate a small chunk of this article to showing you a few tricks for shaping your volumetric light in any way you want.

My friend @KennyPirman does this wonderfully well in his habitat demo scene project, a 3D reconstruction of an O’Neil cylindrical world floating in space.

In it, he shapes the volumetric light/fog and other atmospheric effects as a cylinder using its corresponding Signed Distance Function to fit his specific use case, and we can do the same in our example:

Using SDFs to shape our raymarched light

float sdCylinder(vec3 p, vec3 axisOrigin, vec3 axisDir, float radius) {
  vec3 p_to_origin = p - axisOrigin;
  float projectionLength = dot(p_to_origin, axisDir);
  vec3 closestPointOnAxis = axisOrigin + axisDir * projectionLength;
  float distanceToAxis = length(p - closestPointOnAxis);
  return distanceToAxis - radius;
}

float smoothEdgeWidth = 0.1;

void mainImage(const in vec4 inputColor, const in vec2 uv, out vec4 outputColor) {
  // ...
  for (int i = 0; i < NUM_STEPS; i++) {
    vec3 samplePos = rayOrigin + rayDir * t;

    if (t > sceneDepth || t > cameraFar) {
      break;
    }

    float sdfVal = sdCylinder(samplePos, lightPos, lightDir, 2.0);
    float shapeFactor = smoothstep(0.0, -smoothEdgeWidth, sdfVal);

    if (shapeFactor < 0.1) {
      t += STEP_SIZE;
      continue;
    }

    // ...
  }
}
We can leverage any SDF in our toolset 
3
, the same ones that we explored in my Raymarching blog post 2 years ago, to shape our light as we see fit. The demo below showcases a few examples of lights shaped like cones, spheres, cylinders, and toruses.

Shadow Mapping
Having established the foundation of our volumetric lighting effect, we can now dive into the more complex aspect of this article: computing shadows. In this part, you will see how to:

Leverage once again coordinate systems, however, this time, to go from world space back to screen space.
Create a shadow map of our scene.
Use both concepts to stop drawing our light when occluded by objects in the scene.
Creating a shadow map of our scene
Our current implementation of volumetric lighting does not take the shadows cast by objects placed in the way of our light into account. Realistically, we should see dark streaks or bands where the light is occluded. Moreover, akin to depth-based stopping, the lack of shadow handling results in useless operations: we draw volumetric light where no light should be present.

The diagram below illustrates what we have now compared to what we should expect from an accurate volumetric light:

Diagram showcasing the expected result of handling occlusion from objects within the scene.
Diagram showcasing the expected result of handling occlusion from objects within the scene.
To solve this, we will need to generate a shadow map of our scene: a texture representing the depth of the scene from the point of view of our light. We can achieve this by leveraging the same trick we used for caustics 
4
 back in early 2024 to extract the normals of our object:

We create a dedicated render target for our shadows
We create a dedicated virtual camera and place it in the same position as our light.
We render the scene using this camera.
Since we need the depth of our scene, we need to use Three.js DepthTexture and the depth option set to true when creating our render target. We should also assign a resolution to the resulting depth texture. By default, I chose 256 x 256; remember that the bigger the shadow map, the more intensive our raymarching loop will be.

For our light camera, we need to adjust its field of view or fov attribute accordingly. In our case, we can base its value on the coneAngle or radius of our volumetric light, depending on whether you are respectively using a conal or cylindrical-shaped light.

Setting up our light camera and shadow FBO

// ...

const lightCamera = useMemo(() => {
  const cam = new THREE.PerspectiveCamera(90, 1.0, 0.1, 100);
  cam.fov = coneAngle;
  return cam;
}, [coneAngle]);

const shadowFBO = useFBO(shadowMapSize, shadowMapSize, {
  depth: true,
  depthTexture: new THREE.DepthTexture(
    shadowMapSize,
    shadowMapSize,
    THREE.FloatType
  ),
});

const lightPosition = useRef(new THREE.Vector3(4.0, 4.0, -4.0));
const lightDirection = useRef(
  new THREE.Vector3().copy(lightPosition.current).negate().normalize()
);

useFrame((state) => {
  const { gl, camera, scene, clock } = state;

  lightCamera.position.copy(lightPosition.current);
  lightCamera.updateMatrixWorld();
  lightCamera.updateProjectionMatrix();

  const currentRenderTarget = gl.getRenderTarget();

  gl.setRenderTarget(shadowFBO);
  gl.clear(false, true, false);
  gl.render(scene, lightCamera);

  gl.setRenderTarget(currentRenderTarget);
  gl.render(scene, camera);

  // ...
});

// ...
We also need to ensure the light camera faces the correct direction: the light direction. We can achieve this using the lookAt property:

// ...
const currentLightTargetPos = new THREE.Vector3().addVectors(
  lightPosition.current,
  lightDirection.current
);

lightCamera.lookAt(currentLightTargetPos);
lightCamera.position.copy(lightPosition.current);
lightCamera.updateMatrixWorld();
lightCamera.updateProjectionMatrix();
// ...
I also recommend using a cameraHelper to visualize the camera's view frustum, direction, and field of view properties. This can be useful when debugging strange behaviors with your light camera and its resulting render target.

With this valuable depth data in our hands, we can leverage it within our raymarching loop and stop it from drawing pixels whenever our light gets occluded.

Calculating shadows and occlusion
In this section, we will go through the implementation of a function calculateShadow that, given a certain point in world space, returns:

1.0 if the point is not in shadow
0.0 if the point is in shadow
To achieve that result, we first need to take that point and transform its coordinates back to screen space using the projection and view matrices of the lightCamera we just created.

Transforming the current sampled point to screen space

uniform sampler2D shadowMap;
uniform mat4 lightViewMatrix;
uniform mat4 lightProjectionMatrix;
uniform float shadowBias;

float calculateShadow(vec3 worldPosition) {
  vec4 lightClipPos = lightProjectionMatrix * lightViewMatrix * vec4(worldPosition, 1.0);
  vec3 lightNDC = lightClipPos.xyz / lightClipPos.w;

  vec2 shadowCoord = lightNDC.xy * 0.5 + 0.5;
  float lightDepth = lightNDC.z * 0.5 + 0.5;

  // ...

}
We can see in the code snippet above that this process is simply the reverse of what we did for our first light ray. Here:

shadowCoord represents the uv coordinates we will use to sample from our shadowMap texture.
lightDepth represents the depth of the current pixel from the point of view of our lightCamera.
With those coordinates established, we can first handle some edge cases: if the point is outside of the lightCamera's frustum, we will consider the point as lit, whether it falls outside of a valid UV coordinate range or lies beyond the camera's far plane.

Detecting occlusion - Edge cases

float calculateShadow(vec3 worldPosition) {
  vec4 lightClipPos = lightProjectionMatrix * lightViewMatrix * vec4(worldPosition, 1.0);
  vec3 lightNDC = lightClipPos.xyz / lightClipPos.w;

  vec2 shadowCoord = lightNDC.xy * 0.5 + 0.5;
  float lightDepth = lightNDC.z * 0.5 + 0.5;

  if (
    shadowCoord.x < 0.0 ||
    shadowCoord.x > 1.0 ||
    shadowCoord.y < 0.0 ||
    shadowCoord.y > 1.0 ||
    lightDepth > 1.0
  ) {
    return 1.0;
  }

  // ...

}
We can now focus on the core of the shadow logic. First, we need to sample the shadowMap using the UV coordinates defined via shadowCoord. The resulting color represents the depth of the closest surface visible from the point of view of the lightCamera at that given pixel. Given that it is a grayscale texture, we can consider a single color channel and compare it to the lightDepth:

If the lightDepth at that given pixel is larger than the shadowMapDepth, the point is in shadow.
Else, the point is not in shadow.
Detecting occlusion

float calculateShadow(vec3 worldPosition) {
  vec4 lightClipPos = lightProjectionMatrix * lightViewMatrix * vec4(worldPosition, 1.0);
  vec3 lightNDC = lightClipPos.xyz / lightClipPos.w;

  vec2 shadowCoord = lightNDC.xy * 0.5 + 0.5;
  float lightDepth = lightNDC.z * 0.5 + 0.5;

  if (
    shadowCoord.x < 0.0 ||
    shadowCoord.x > 1.0 ||
    shadowCoord.y < 0.0 ||
    shadowCoord.y > 1.0 ||
    lightDepth > 1.0
  ) {
    return 1.0;
  }

  float shadowMapDepth = texture2D(shadowMap, shadowCoord).x;

  if (lightDepth > shadowMapDepth + shadowBias) {
    return 0.0;
  }

  return 1.0;
}
The diagram below illustrates the specific aspects and edge cases of our current setup.

Diagram showcasing sampling points being in shadow, within/outside the lightCamera frustum, and lit.
Diagram showcasing sampling points being in shadow, within/outside the lightCamera frustum, and lit.
You can visualize what our lightCamera sees by outputting the result of this function and returning it as the final color of our effect:

float shadow = calculateShadow(worldPosition);

outputColor = vec4(vec3(shadow), 1.0);
Notice how blocky the sphere in our scene appears in this case. That is due to the resolution of the shadowMap. A higher resolution yields a more detailed shadowMap, which results in more accurate shadows in the final render.

Before/After comparison of our shadow map with a resolution of 128x128 and 1024x1024.
Once defined, we can leverage this function as a skip condition in our raymarching loop. Why a skip? Because points beyond the current sampled point in shadow may not be occluded. Thus, we keep marching our ray and sampling in case we need to accumulate more light later on.

Diagram illustrating why we're skipping instead of interrupting our raymarching loop.
Diagram illustrating why we're skipping instead of interrupting our raymarching loop.
Taking shadows into account while raymarching

// ...
for (int i = 0; i < NUM_STEPS; i++) {
  vec3 samplePos = rayOrigin + rayDir * t;

  if (t > sceneDepth || t > cameraFar) {
    break;
  }

  float shadowFactor = calculateShadow(samplePos);
  if (shadowFactor == 0.0) {
    t += STEP_SIZE;
    continue;
  }

  // ...
  t += STEP_SIZE
}

// ...
Once integrated into our original demo, we can observe some beautiful shadow beams as a result of the volumetric light being blocked by our sphere.

App.js
fragmentShader.glsl
uniform mat4 projectionMatrixInverse;
uniform vec3 lightDirection;
uniform vec3 lightPosition;
uniform mat4 viewMatrixInverse;
uniform vec3 cameraPosition;
uniform float cameraFar;
uniform float coneAngle;
uniform int shapeType;

uniform sampler2D shadowMap;
uniform mat4 lightViewMatrix;
uniform mat4 lightProjectionMatrix;
uniform float shadowBias;

float readDepth(sampler2D depthSampler, vec2 coord) {
  return texture2D(depthSampler, coord).x;
}

vec3 getWorldPosition(vec2 uv, float depth) {
  float clipZ = depth * 2.0 - 1.0;
  vec2 ndc = uv * 2.0 - 1.0;
  vec4 clip = vec4(ndc, clipZ, 1.0);
  vec4 view = projectionMatrixInverse * clip;
  vec4 world = viewMatrixInverse * view;

  return world.xyz / world.w;
}

float calculateShadow(vec3 worldPosition) {
  vec4 lightClipPos = lightProjectionMatrix * lightViewMatrix * vec4(worldPosition, 1.0);
  vec3 lightNDC = lightClipPos.xyz / lightClipPos.w;

  vec2 shadowCoord = lightNDC.xy * 0.5 + 0.5;
  float lightDepth = lightNDC.z * 0.5 + 0.5;

  if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 || shadowCoord.y < 0.0 || shadowCoord.y > 1.0 || lightDepth > 1.0) {
Preview
Console

Light Scattering and other improvements
We now have all the building blocks for a beautiful volumetric light effect: a shaped light accumulating through a volume with shadow beams appearing when occluded. It is time to add the little details and improvements to our post-processing shader that will not only make our light appear more realistic but also more performant.

Phase function and noise
Currently, our light has the following attenuation function: float attenuation = exp(-0.05 * distanceTofLight);. While this distance-based attenuation helped us get started, there are better ways to simulate light propagating through a volume.

First, we can introduce directional scattering using the Henyey-Greenstein function. I covered this function in lengths in Real-time dreamy Cloudscapes with Volumetric Raymarching when attempting to improve the way light gets accumulated within raymarched clouds. The function has the same purpose here. It will help us yield more realistic lighting throughout our volume:

Henyey-Greenstein phase function

float HGPhase(float mu) {
  float g = SCATTERING_ANISO;
  float gg = g * g;

  float denom = 1.0 + gg - 2.0 * g * mu;
  denom = max(denom, 0.0001);


  float scatter = (1.0 - gg) / pow(denom, 1.5);
  return scatter;
}
We can include the result of the phase function when computing the luminance / light contribution of the current step in our raymarching loop.

Luminance

// ...

for (int i = 0; i < NUM_STEPS; i++) {
  vec3 samplePos = rayOrigin + rayDir * t;
  // Stop sampling when camera far or scene depth is reached
  if (t > sceneDepth || t > cameraFar) {
    break;
  }

  // Handling shadows/occlusion
  float shadowFactor = calculateShadow(samplePos);
  if (shadowFactor == 0.0) {
    t += STEP_SIZE;
    continue;
  }

  // Shaping the light via SDF
  float sdfVal = sdCone(samplePos, lightPos, lightDir, halfConeAngleRad);
  float shapeFactor = smoothstep(0.0, -smoothEdgeWidth, sdfVal);

  if (shapeFactor < 0.1) {
    t += STEP_SIZE;
    continue;
  }

  float distanceToLight = length(samplePos - lightPos);
  vec3 sampleLightDir = normalize(samplePos - lightPos);

  float attenuation = exp(-0.3 * distanceToLight);
  float scatterPhase = HGPhase(dot(rayDir, -sampleLightDir));
  vec3 luminance = lightColor * LIGHT_INTENSITY * attenuation * scatterPhase;

  // ...
  t += STEP_SIZE
}

// ...
The next step is to tweak how much light gets scattered through:

stepDensity: a variable that simulates the amount of fog
stepTransmittance: a variable that represents the amount of light that gets absorbed by the medium/fog using Beers' Law.
Accumulated light

// ...
void mainImage(const in vec4 inputColor, const in vec2 uv, out vec4 outputColor) {
  // ...

  float transmittance = 5.0;
  vec3 accumulatedLight = vec3(0.0);

  for (int i = 0; i < NUM_STEPS; i++) {
    // ...

    float distanceToLight = length(samplePos - lightPos);
    vec3 sampleLightDir = normalize(samplePos - lightPos);

    float attenuation = exp(-0.3 * distanceToLight);
    float scatterPhase = HGPhase(dot(rayDir, -sampleLightDir));
    vec3 luminance = lightColor * LIGHT_INTENSITY * attenuation * scatterPhase;

    float stepDensity = FOG_DENSITY * shapeFactor;
    stepDensity = max(stepDensity, 0.0);

    float stepTransmittance = BeersLaw(stepDensity * STEP_SIZE, 1.0);
    transmittance *= stepTransmittance;
    accumulatedLight += luminance * transmittance * stepDensity * STEP_SIZE;

    t += STEP_SIZE;
  }

  vec3 finalColor = inputColor.rgb + accumulatedLight;

  outputColor = vec4(finalColor, 1.0);
}
Beers' Law states that the intensity of light passing through a transparent medium is exponentially related to the distance it travels. The further to the medium light propagates, the more it is being absorbed.

With this, we now have a more realistic light propagating through a medium with consistent density. The demo below combines the code we just covered into our original scene:

Of course, to yield a more moody result, we could opt for a more dynamic and organic medium for our light to propagate through, like a thicker fog of soft clouds. To do so, we can add some Fractal Brownian Motion, which I introduced in both my raymarching and volumetric raymarching posts, allowing us to render complex noise patterns that mimic the variations of density we can find in those atmospheric effects.

Adding fog-like noise to our effect

// ...

const float NOISE_FREQUENCY = 0.5;
const float NOISE_AMPLITUDE = 10.0;
const int NOISE_OCTAVES = 3;

float fbm(vec3 p) {
  vec3 q = p + time * 0.5 * vec3(1.0, -0.2, -1.0);
  float g = noise(q);

  float f = 0.0;
  float scale = NOISE_FREQUENCY;
  float factor = NOISE_AMPLITUDE;

  for (int i = 0; i < NOISE_OCTAVES; i++) {
      f += scale * noise(q);
      q *= factor;
      factor += 0.21;
      scale *= 0.5;
  }

  return f;
}

void mainImage(const in vec4 inputColor, const in vec2 uv, out vec4 outputColor) {
  // ...
  for (int i = 0; i < NUM_STEPS; i++) {
    // ...

    // Shaping the light via SDF
    float sdfVal = sdCone(samplePos, lightPos, lightDir, halfConeAngleRad);
    float shapeFactor = -sdfVal + fbm(samplePos); // you can also name this "density"

    if (shapeFactor < 0.1) {
        t += STEP_SIZE;
        continue;
    }

    //...
  }
  // ...
}

// ...
The demo below integrates this small yet essential add-on to our volumetric lighting effect, giving the impression of a powerful beam of light cutting through thick fog.

Performance improvements
As with many of my raymarching experiments, the main performance issues often come from the granularity of the raymarching loop, specifically the size of each step. In our current setup, each step of the loop triggers several heavy processes such as sampling textures until we reach the maximum amount of steps. To alleviate that, we could:

Reduce the maximum number of steps, but this would lead to stopping sampling light too early and ending with less depth in our volumetric light.
Increase the step size to reach the maximum amount of steps quicker, however, this would lead to visible banding and the quality of the output would decrease.
Screenshot of our scene where the step size was reduced, yielding some visible banding for our volumetric light.
Screenshot of our scene where the step size was reduced, yielding some visible banding for our volumetric light.
To work around those artifacts while reducing the performance impact of our raymarching loop, we can introduce some blue noise dithering. I briefly mentioned this technique in my article on volumetric clouds. As you may have guessed, it is also applicable here!

The principle remains the same: we introduce a random offset, that we get from a blue noise texture, to each of our rays erasing any visible artifacts like banding and yielding a cleaner result.

Diagram illustrating the offset introduced by blue noise dithering.
Diagram illustrating the offset introduced by blue noise dithering.
Moreover, by slightly shifting the noise pattern on every frame we can increase the quality of our output and make the dithering pattern from the noise almost unnoticeable.

Adding Blue Noise Dithering

// ...
uniform sampler2D blueNoiseTexture;
uniform int frame;

//...
void mainImage(const in vec4 inputColor, const in vec2 uv, out vec4 outputColor) {
  // ...
  float blueNoise = texture2D(blueNoiseTexture, gl_FragCoord.xy / 1024.0).r;
  float offset = fract(blueNoise + float(frame%32) / sqrt(0.5));
  float t = STEP_SIZE * offset;

  for (int i = 0; i < NUM_STEPS; i++) {
    vec3 samplePos = rayOrigin + rayDir * t;
    // ...
  }
  // ...
}
The demo below implements this method. In it I:

decreased NUM_STEPS from 250 to 50
increased STEP_SIZE from 0.05 to 0.5
Thus going from 5000 iterations to 100 with a similar output quality.

Elevating your work with light
With all the details of our volumetric lighting post-processing effect implemented, we can now focus on a few applications of this effect and observe how it can significantly alter the look and feel of a scene. You may have seen this effect featured in many video games or digital art pieces as a way to flood the scene with atmospheric light, filling the air with beams of light and shadows, thus making light more like an asset and not just "hitting surfaces".

In this section, I wanted to offer a breakdown of some of my creations that leverage volumetric lighting where it really shines (no pun intended) to bring that same bright atmospheric vibe onto my work.

Arches
I had this scene in my mind as a goal the very moment I started looking into this topic. I visualized this tall yet narrow series of arched doors where beams of light could shine through. The columns located between each door would cast striking shadows against the bright backdrop from the light on the other side.

Diagram representing three arched doors with light beaming through the opening, and the wall/columns casting shadows.
Diagram representing three arched doors with light beaming through the opening, and the wall/columns casting shadows.
Luckily, the implementation of the effect allowed me to realize this vision:

The volumetric raymarching aspect allows us to give our light the shape necessary to be visible through the air while shining through the door.
The shadow map of the scene will ensure the beams of light are occluded where needed on the scene, especially when hitting the columns separating the different arches.
The support for fog/clouds can give a mystical look and feel to the scene while emphasizing the light.
I got the idea of adding stairs to the scene from a similar project on Spline by Vlad Ponomarenko. These are simple CylinderGeometry stacked on top of one another with decreasing radius. I did not even bother making those instances as they are only a few steps (maybe I should have).

To top it all off, I added a lighter gray Backdrop to the scene to create a high level of contrast between the camera's location (darker space) and the space beyond the doors.

I also added additional effects on top of the volumetric lighting one such as:

A powerful Bloom to further increase the contrast between light and darkness.
A grainy Noise to add some more texture to the final render.
The demo below contains all those little details that make the scene beautiful yet so moody at the same time. It uses the same effect we built in this article and is a great way to see the difference volumetric lighting can make.

Asteroid Belt
I love a good space-themed 3D scene, and always need a good excuse to work on one. Here, it felt natural to explore how volumetric lighting could amplify the brightness of a sun or the darkness of planets eclipsing their star.

This project was originally inspired by a demo scene built by hmans.dev which can be found here.

I reused the asteroid model he built and featured in this scene in my original implementation and the demo included below in this article.

The principle behind this scene remains relatively simple:

We have a central source of light: a star, which is a sphereGeometry using a meshBasicMaterial. I also positioned at the same coordinate a pointLight so we would have light propagating and shadow cast in every direction.
<mesh ref={lightRef}>
  <pointLight castShadow intensity={500.5} />
  <sphereGeometry args={[0.5, 32]} />
  <meshBasicMaterial color={new THREE.Color('white').multiplyScalar(10)} />
</mesh>
Two asteroid rings, each composed of their own set of instancedMesh using an asteroid geometry and a black meshStandardMaterial. This allowed me to scale up the number of objects in the scene without impacting performance.
Our volumetric lighting effect with a slightly tweaked behavior: the direction of the light will always point from the original position of the light towards us the viewer (camera position). This allowed me to only have light raymarched where relevant/visible, while also giving the impression to the viewer that it is shining in every direction.
On top of that, the light camera will have a large fov of 90 degrees and will always point towards the main camera. Thus, no matter our point of view, we will have the widest possible shadow map of our scene and see beams of shadows from the volumetric light being occluded by the many asteroids in our scene.
const lightDirection = new THREE.Vector3()
  .subVectors(camera.position, lightPosition)
  .normalize();

lightCamera.position.copy(lightPosition);
lightCamera.lookAt(camera.position);
lightCamera.updateMatrixWorld();
lightCamera.updateProjectionMatrix();
I also tuned out any atmospheric effects for more realism, hence the FOG_INTENSITY is turned down to 0.02.
This, combined with a high-resolution shadow map, results in a beautiful space scene, leveraging volumetric lighting to make the light emitted from the star feel more bright and powerful.

You may notice the shadow beams flickering at first. This is a side-effect of the shadow map resolution. Try a higher resolution and observe how the effect becomes more stable. This type of artifact tends to happen in any scene featuring a large number of objects occluding the light, even with simple geometries. I had a similar issue when attempting to reproduce the gorgeous work of @5tr4n0 a digital artist leveraging volumetric lighting beautifully.


MaximeHeckel
Maxime
@MaximeHeckel
got tempted to rebuild this in webgl using my volumetric lighting shader work

not as good but still a fun one to build

https://t.co/c0Q8rL0GA0 https://t.co/PmkI0IKvku

25
1317
7:07 PM - May 2, 2025
On my end, I tend to settle for a shadow map of 512x512px to strike the right balance between performance and a minimum amount of shadow artifact.

Beyond
With our effect fully implemented and operational on the few demo scenes we just went through you may be wondering: what's next?. The volumetric lighting post-processing shader we built bit-by-bit throughout this article may look relatively polished, there are still, however, a couple of pitfalls worth mentioning that we do not handle as of now:

Throughout this article, we only ever considered one single source of light, the effect has no way to handle multiple sources at the moment.
The shadows are only ever observable in the direction of the light. This is very apparent in the asteroid demo, and why I opted to have the lightCamera always pointing towards the camera to hide this limitation. But, what if we wanted a volumetric point light that could cast shadows in multiple directions?
This last section aims to answer those questions and provide a few examples I implemented to explore solutions. It also serves as the conclusion of our exploration of volumetric lighting.

To answer the first issue, scaling up to multiple light sources is not much trouble besides requiring a lot of extra code for each new source of light:

new lighCamera
new shadowFBO
passing each texture, matrices, and other arguments to the effect
calculating the shadows, light scattering, and light accumulation
Once done, the only thing to do is to combine the light accumulation of each light and we have our volumetric lighting with multiple light sources working. The demo below showcases a complete working example with two lights, which you will see, is quite a bit longer than any example we've seen so far:

Unfortunately, my solution to the second pitfall required quite a bit of refactoring, and the result is, at best, a cool hack.

We know that the process behind our shadow beams relies on a lightCamera pointing in a given direction. Its field of view is limited and thus, we can't capture the shadowMap of the entire scene. To do so, we would need a camera in every direction: top, bottom, left, right, front, back, and as many dedicated FBOs. That's a lot of code. Too much. Luckily for us, Three.js has a neat utility called a CubeCamera that bundles together six cameras and a WebGLCubeRenderTarget for us to store and read the resulting texture.

Diagram showcasing a cube camera positioned at the center of a scene and the respective direction of each of its faces.
Diagram showcasing a cube camera positioned at the center of a scene and the respective direction of each of its faces.
Setting up a CubeCamera and WebGLCubeRenderTarget

const shadowCubeRenderTarget = useMemo(() => {
  const rt = new THREE.WebGLCubeRenderTarget(SHADOW_MAP_SIZE, {
    format: THREE.RGBAFormat,
    type: THREE.FloatType,
    generateMipmaps: false,
    minFilter: THREE.LinearFilter,
    magFilter: THREE.LinearFilter,
    depthBuffer: true,
  });
  return rt;
}, []);

const shadowCubeCamera = useMemo(() => {
  const cam = new THREE.CubeCamera(
    CUBE_CAMERA_NEAR,
    CUBE_CAMERA_FAR,
    shadowCubeRenderTarget
  );
  return cam;
}, [shadowCubeRenderTarget]);
However, there is an issue that was almost a blocker when I encountered it: there is no CubeDepthTexture. So I had to resolve myself to build my own "depth texture" by:

Implementing a custom shadowMaterial that returns the normalized distance between any objects of the scene and a light position uniform.
Replacing the materials of all objects in the scene with the shadowMaterial.
Take a snapshot of the scene with the cube camera.
Restore the scene to its original state.
Using the shadowMaterial to extract depth data

const shadowMaterial = useMemo(
  () =>
    new THREE.ShaderMaterial({
      vertexShader: \`
        varying vec3 vWorldPosition;
        void main() {
          vec4 worldPosition = modelMatrix * vec4(position, 1.0);
          vWorldPosition = worldPosition.xyz;
          gl_Position = projectionMatrix * viewMatrix * worldPosition;
        }
        \`,
      fragmentShader: \`
        uniform vec3 lightPosition;
        uniform float shadowFar;
        varying vec3 vWorldPosition;

        void main() {
          // Calculate linear distance from the light source
          float distance = length(vWorldPosition);
          // Normalize distance to [0, 1] using the shadow camera's far plane
          float normalizedDistance = clamp(distance / shadowFar, 0.0, 1.0);
          // Store the normalized distance in the red channel.
          gl_FragColor = vec4(normalizedDistance, 0.0, 0.0, 1.0);
        }
        \`,
      side: THREE.DoubleSide,
      uniforms: {
        lightPosition: { value: new THREE.Vector3() },
        shadowFar: { value: CUBE_CAMERA_FAR },
      },
    }),
  [],
);
Then, we only need to pass the resulting shadowMapCube to our effect and its underlying fragment shader and modify our calculateShadow function to handle this new cube texture. After that, we have a volumetric lighting effect that can take into account objects in the entire scene in every direction and cast shadows accordingly. The next and final demo of this article contains my take on this issue. Again, consider this more a hack than a solution for production, as the result it yields is not as good as what we were able to accomplish together in other examples.

I hope you are as pleased as I am with how this topic and the demo scenes built using the new set of techniques turned out. Volumetric lighting felt like the perfect extension to my post-processing work while also expanding on some previous raymarching experiments, and that's why it felt natural for me to talk about it at this moment in time.

Using post-processing as a gateway to more physically accurate 3D work on top of stylization in future work will be an interesting undertaking. I'm also looking forward to diving into adjacent concepts such as global illumination or ambient occlusion, which are still nebulous to me, while also leveraging some of the new tools we learned about here like shadow mapping or virtual/cameras for other purposes. But, thanks to what I learned throughout writing this article, I feel confident I'll be soon able to get back to you with an article on those topics and ever more ambitious 3D work running in your browser to share with you.

Until then, I hope this article will be enough to keep you busy and inspire you to experiment more with volumetric lighting on your end!

1
You can learn more about Perspective Divide here. I also recommend checking out this video from Brandon Berisford who shares how to derive the Perspective Projection Matrix and details what this perspective divide step does.

2
There is also a CONVOLUTION effect attribute which alongside DEPTH give you out-of-the-box special operations for your effect fragment shader. Refer to this documentation for more examples and information.

3
You should definitely check out Inigo Quilez dictionary of SDF and try to have fun shaping your raymarched light with a couple of odd ones even though it would not be realistic.

4
In the case of caustics, we had to swap the material of the mesh with a NormalMaterial to extract the normal data of the object and have it available as a texture.

Liked this article? Share it with a friend on Twitter or support me to take on more ambitious projects to write about. Have a question, feedback or simply wish to contact me privately? Shoot me a DM and I'll do my best to get back to you.

Have a wonderful day.

– Maxime










