# Plan: Fix Tree Selection for Third-Person Camera

## Problem
Currently, the ray casting for tree selection starts from the camera position and goes toward where the camera is looking. In third-person view, this causes trees between the camera and player to be incorrectly selected.

## Solution: Option 1 - Start ray from player position

Cast the ray from the player's position (slightly elevated to eye level) using the camera's look direction. This way trees behind the player or between camera and player won't be selected.

## Files to Modify

### 1. `src/core/main.cpp`
**Current code (~line 291-294):**
```cpp
// Get look direction for tree targeting
Vec3 lookDir = app.camera.getLookDirection();

// Find tree in crosshairs
bool hasTargetedTree = false;
Vec3 targetedTreePos = app.terrain.findTreeInCrosshairs(cameraPos, lookDir, 20.0f, hasTargetedTree);
```

**Change to:**
```cpp
// Get look direction for tree targeting
Vec3 lookDir = app.camera.getLookDirection();

// Find tree in crosshairs - start from player position, not camera
// Use player eye level (position + some height offset)
Vec3 playerEyePos = Vec3(
    app.player.position.x,
    app.player.position.y + 1.5f,  // Eye level
    app.player.position.z
);
bool hasTargetedTree = false;
Vec3 targetedTreePos = app.terrain.findTreeInCrosshairs(playerEyePos, lookDir, 20.0f, hasTargetedTree);
```

## Summary of Changes
- **1 file modified**: `main.cpp`
- **Lines changed**: ~3-5 lines
- Change the first parameter of `findTreeInCrosshairs` from `cameraPos` to player's eye-level position

## Expected Behavior After Fix
- Trees between camera and player will NOT be selected
- Trees in front of the player (in the direction they're looking) WILL be selected
- Selection range is measured from the player, not the camera
