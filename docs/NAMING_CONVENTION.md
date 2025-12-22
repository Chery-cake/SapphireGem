# Object and Material Naming Convention

## Overview

This document describes the standardized naming convention for objects, materials, textures, and buffers in SapphireGem. The `naming.h` header provides helper functions to ensure consistent naming across the codebase.

## Naming Patterns

### Objects

Objects follow the pattern: `<type>_<descriptor>`

Examples:
- `triangle_main` - Main triangle object
- `cube_player` - Player's cube
- `quad_atlas1` - First atlas quad
- `square_textured` - Textured square

Use the helper function:
```cpp
#include "naming.h"

std::string objectName = naming::make_object_name(
    naming::ObjectType::TRIANGLE_2D, 
    "main"
);
// Result: "triangle_main"
```

Available object types:
- `ObjectType::TRIANGLE_2D`
- `ObjectType::SQUARE_2D`
- `ObjectType::CUBE_3D`
- `ObjectType::QUAD_2D`
- `ObjectType::CUSTOM` - For custom names

### Materials

Materials follow established naming patterns based on type and features.

Examples:
- `Test` - Basic test material
- `Test2D` - 2D test material
- `Textured_atlas` - Textured material with atlas support
- `Textured3D_checkerboard` - 3D textured checkerboard material

Use the helper function:
```cpp
std::string materialName = naming::make_material_name(
    naming::MaterialType::TEXTURED_ATLAS
);
// Result: "Textured_atlas"
```

Available material types:
- `MaterialType::TEST`
- `MaterialType::TEST_2D`
- `MaterialType::TEST_3D_TEXTURED`
- `MaterialType::TEXTURED`
- `MaterialType::TEXTURED_CHECKERBOARD`
- `MaterialType::TEXTURED_GRADIENT`
- `MaterialType::TEXTURED_ATLAS`
- `MaterialType::TEXTURED_3D_CHECKERBOARD`
- `MaterialType::TEXTURED_3D_GRADIENT`
- `MaterialType::TEXTURED_3D_ATLAS`
- `MaterialType::CUSTOM`

### Textures

Textures use simple descriptive names:
- `checkerboard`
- `gradient`
- `atlas`

Use the helper function:
```cpp
std::string textureName = naming::make_texture_name("checkerboard");
// Result: "checkerboard"
```

### Buffers

Buffers follow the pattern: `<material>_<object>_<type>`

Examples:
- `Textured_atlas_quad1_ubo` - UBO for atlas quad 1
- `Test_cube_vertices` - Vertex buffer for test cube

Use the helper function:
```cpp
std::string bufferName = naming::make_buffer_name(
    "Textured_atlas",
    "quad1",
    "ubo"
);
// Result: "Textured_atlas_quad1_ubo"
```

## Reload System

The object and material manager now supports proper cleanup and recreation during reload operations.

### How Reload Works

1. When R or F key is pressed, `Renderer::reload()` is called
2. The renderer cleans up resources in reverse dependency order:
   - ObjectManager (destroys all objects)
   - BufferManager
   - TextureManager
   - MaterialManager
   - DeviceManager
3. Resources are recreated in dependency order
4. A post-reload callback is invoked to recreate objects
5. The Window recreates all scene objects with fresh pointers

### Setting Up Reload Callback

In your window or application class:

```cpp
renderer->set_post_reload_callback([this]() {
    std::print("Recreating scene objects after reload...\n");
    create_scene_objects();
});
```

### Object Creation Function

Your `create_scene_objects()` function should:
1. Reset all object pointers to nullptr
2. Recreate all objects using the standardized naming
3. Reconfigure object properties (rotation mode, etc.)

Example:
```cpp
void Window::create_scene_objects() {
    // Reset pointers
    triangle = nullptr;
    cube = nullptr;
    
    // Recreate objects
    triangle = renderer->create_triangle_2d(
        naming::make_object_name(naming::ObjectType::TRIANGLE_2D, "main"),
        position, rotation, scale
    );
    
    cube = renderer->create_cube_3d(
        naming::make_object_name(naming::ObjectType::CUBE_3D, "main"),
        position, rotation, scale
    );
}
```

## Benefits

1. **Consistency**: All objects follow the same naming pattern
2. **Maintainability**: Easy to understand and find objects by name
3. **Debuggability**: Clear object names in logs and error messages
4. **Reload Safety**: Objects can be safely destroyed and recreated
5. **Type Safety**: Enum-based naming reduces typos

## Migration Guide

To migrate existing code to use the new naming system:

1. Include the naming header:
   ```cpp
   #include "naming.h"
   ```

2. Replace hardcoded names with helper functions:
   ```cpp
   // Before:
   auto obj = create_object("triangle");
   
   // After:
   auto obj = create_object(
       naming::make_object_name(naming::ObjectType::TRIANGLE_2D, "main")
   );
   ```

3. Update material references:
   ```cpp
   // Before:
   .materialIdentifier = "Textured_atlas"
   
   // After:
   .materialIdentifier = naming::make_material_name(
       naming::MaterialType::TEXTURED_ATLAS
   )
   ```

4. Set up reload callback in constructor:
   ```cpp
   renderer->set_post_reload_callback([this]() {
       create_scene_objects();
   });
   ```

5. Ensure create_scene_objects() resets pointers before recreation
