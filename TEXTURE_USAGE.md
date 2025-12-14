# Image Loading and Texture System Usage Guide

This document describes how to use the image loading and texture system in SapphireGem.

## Overview

The system provides three main classes:
- **Image**: Loads and manipulates image data (PNG, JPG, GIF)
- **Texture**: Manages textures with support for single textures and texture atlases
- **TextureManager**: Manages texture lifecycle and caching

## Basic Usage

### Loading a Simple Texture

```cpp
// Get the texture manager from the renderer
TextureManager& textureMgr = renderer.get_texture_manager();

// Create and load a texture
Texture* myTexture = textureMgr.create_texture("my_texture", "path/to/image.png");

// The texture is now loaded and uploaded to the GPU
```

### Loading a Texture Atlas

```cpp
// Create a texture atlas with a 4x4 grid
Texture* atlas = textureMgr.create_texture_atlas(
    "sprite_atlas",          // Identifier
    "path/to/spritesheet.png", // Image file path
    4,                        // Rows
    4                         // Columns
);

// Access a specific region from the atlas
const Texture::AtlasRegion* region = atlas->get_atlas_region("tile_0_0");
```

## Image Manipulation

### Color Tinting

```cpp
Texture* texture = textureMgr.get_texture("my_texture");

// Apply a green tint (multiply RGB channels)
texture->set_color_tint(glm::vec4(0.5f, 1.0f, 0.5f, 1.0f));

// Update GPU with the modified image data
texture->update_gpu();
```

### Rotation

```cpp
Texture* texture = textureMgr.get_texture("my_texture");

// Rotate 90 degrees clockwise
texture->rotate_90_clockwise();

// Or rotate 90 degrees counter-clockwise
texture->rotate_90_counter_clockwise();

// Or rotate 180 degrees
texture->rotate_180();

// Update GPU with the modified image data
texture->update_gpu();
```

## Direct Image Manipulation

For more control, you can work directly with the Image class:

```cpp
// Get the image from a texture
std::shared_ptr<Image> img = texture->get_image();

// Manipulate the image
img->set_color_tint(glm::vec4(1.0f, 0.5f, 0.5f, 1.0f));
img->rotate_90_clockwise();

// Upload changes to GPU
img->update_gpu_data();
```

## Creating Textures Manually

```cpp
// Create texture info
Texture::TextureCreateInfo createInfo;
createInfo.identifier = "custom_texture";
createInfo.type = Texture::TextureType::SINGLE;
createInfo.imagePath = "path/to/image.png";

// Create the texture
Texture* texture = textureMgr.create_texture(createInfo);
```

## Using Textures as Materials

While the basic texture loading system is now in place, to use textures on 3D objects' surfaces, you would need to:

1. Extend the vertex format to include UV coordinates
2. Create shaders that sample textures
3. Bind textures to descriptor sets in materials
4. Update RenderObject to support textured vertices

This foundational system provides the building blocks for texture-based rendering.

## Supported Image Formats

The system uses stb_image and supports:
- PNG
- JPG/JPEG
- GIF
- BMP
- TGA
- PSD
- And many other formats supported by stb_image

## Multi-GPU Support

The texture system automatically handles multi-GPU scenarios. Textures are uploaded to all logical devices managed by the DeviceManager.

## Example: Complete Workflow

```cpp
// Initialize renderer
Renderer renderer(window);

// Load a texture
TextureManager& textureMgr = renderer.get_texture_manager();
Texture* texture = textureMgr.create_texture("player_sprite", "sprites/player.png");

// Manipulate it
texture->set_color_tint(glm::vec4(1.0f, 0.8f, 0.8f, 1.0f)); // Slight red tint
texture->update_gpu();

// Create an atlas for tiles
Texture* tileAtlas = textureMgr.create_texture_atlas(
    "tiles",
    "textures/tileset.png",
    8, 8  // 8x8 grid
);

// Access a specific tile
const Texture::AtlasRegion* grass = tileAtlas->get_atlas_region("tile_2_3");
if (grass) {
    // Use grass->uvMin and grass->uvMax for texture coordinates
}

// Clean up is automatic when the renderer is destroyed
```

## Notes

- All texture operations are thread-safe through internal mutexes
- Textures are automatically cleaned up when the TextureManager is destroyed
- Image data is cached in CPU memory until explicitly released
- GPU uploads are handled automatically through Vulkan staging buffers
