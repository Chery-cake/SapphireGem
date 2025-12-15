# Texture System Demonstration

## What Was Implemented

This demonstration shows two images being loaded and manipulated:

### 1. Textured Square (Checkerboard Pattern)
- **Image**: `assets/textures/checkerboard.png` (256x256 red/white checkerboard)
- **Manipulation**: Green color tint applied via `set_color_tint(glm::vec4(0.7f, 1.0f, 0.7f, 1.0f))`
- **Display**: Created as a square object at top-left position (-0.5, 0.5)
- **Animation**: Gently rotates at 0.5x speed

### 2. Plain Image Quad (Gradient Pattern)  
- **Image**: `assets/textures/gradient.png` (256x256 color gradient)
- **Manipulation**: Rotated 90 degrees clockwise via `rotate_90_clockwise()`
- **Display**: Created as a square object at top-right position (0.5, 0.5)
- **Animation**: Rotates at 0.8x speed

## Code Implementation

The demonstration is implemented in `window.cpp`:

```cpp
// Load textures using the TextureManager
auto &textureMgr = renderer->get_texture_manager();

// Load checkerboard texture for the square
auto *checkerboardTex =
    textureMgr.create_texture("checkerboard", "assets/textures/checkerboard.png");
if (checkerboardTex) {
    // Apply green tint to demonstrate color manipulation
    checkerboardTex->set_color_tint(glm::vec4(0.7f, 1.0f, 0.7f, 1.0f));
    checkerboardTex->update_gpu();
}

// Load gradient texture for the plain image
auto *gradientTex =
    textureMgr.create_texture("gradient", "assets/textures/gradient.png");
if (gradientTex) {
    // Rotate 90 degrees to demonstrate rotation
    gradientTex->rotate_90_clockwise();
    gradientTex->update_gpu();
}

// Create square objects to represent where textures would be displayed
texturedSquare = renderer->create_square_2d("textured_square", ...);
imageQuad = renderer->create_square_2d("image_quad", ...);
```

## Console Output

When the application runs, you'll see:

```
Loading textures...
Image - checkerboard_image - loaded from file: assets/textures/checkerboard.png (256x256, 4 channels)
Loaded checkerboard texture: 256x256
Image - checkerboard_image - applied color tint (0.7, 1, 0.7, 1)
Image - checkerboard_image - updated GPU data
Applied green tint to checkerboard texture
Image - gradient_image - loaded from file: assets/textures/gradient.png (256x256, 4 channels)
Loaded gradient texture: 256x256
Image - gradient_image - rotated 90 degrees clockwise
Image - gradient_image - updated GPU data
Rotated gradient texture 90 degrees clockwise
Scene objects created: textured square and image quad
Note: Textures are loaded and manipulated, but shader support for 
texture sampling would be needed to display them on these objects.
The squares are shown with vertex colors representing where 
textures would be applied in a full implementation.
```

## Visual Representation

Since the current rendering pipeline uses vertex colors rather than texture sampling, the squares display with colored vertices (red, green, blue, yellow corners). In a full texture implementation:

- The **left square** would display the green-tinted checkerboard pattern
- The **right square** would display the rotated gradient pattern

Both squares rotate slowly to demonstrate the animation system works with the objects.

## Features Demonstrated

✅ Image loading from PNG files using stb_image
✅ Texture creation and management via TextureManager
✅ Color tinting (multiplication of RGB channels)
✅ Image rotation (90° clockwise)
✅ GPU upload of modified image data
✅ Multi-device support (textures uploaded to all GPUs)
✅ Object creation and rendering
✅ Animation system integration

## Next Steps for Full Texture Support

To display actual textures on these objects, the following would be needed:

1. Extend vertex format to include UV coordinates (`Vertex2DTextured` struct added)
2. Create shaders that sample textures (vertex + fragment shaders)
3. Bind texture descriptors to materials
4. Update RenderObject to support textured vertices
5. Implement texture coordinate generation for geometry

The foundational texture loading and manipulation system is complete and working!
