# 64 位整数接口完整清单（Godot 4.7.2.stable.official）

> 由 `project/extension_api.json` 直接导出；摘要与结论见 `64bit-api-surface.md`。

## 3. 返回 64 位的类方法（247 个）

### 3.1 `uint64`（115 个）

| 方法 | 静态 |
|---|---|
| `AnimationNode.get_processing_animation_tree_instance_id` |  |
| `CameraFeed.get_texture_tex_id` |  |
| `DirAccess.get_space_left` |  |
| `DisplayServer.window_get_attached_instance_id` |  |
| `EditorExportPlugin._get_customization_configuration_hash` |  |
| `EncodedObjectAsID.get_object_id` |  |
| `Engine.get_physics_frames` |  |
| `Engine.get_process_frames` |  |
| `ExternalTexture.get_external_texture_id` |  |
| `FileAccess.get_64` |  |
| `FileAccess.get_access_time` | static |
| `FileAccess.get_length` |  |
| `FileAccess.get_modified_time` | static |
| `FileAccess.get_position` |  |
| `ImporterMesh.get_surface_format` |  |
| `KinematicCollision2D.get_collider_id` |  |
| `KinematicCollision3D.get_collider_id` |  |
| `MeshDataTool.get_format` |  |
| `NavigationServer2D.link_get_owner_id` |  |
| `NavigationServer2D.region_get_owner_id` |  |
| `NavigationServer3D.link_get_owner_id` |  |
| `NavigationServer3D.region_get_owner_id` |  |
| `OS.get_main_thread_id` |  |
| `OS.get_static_memory_peak_usage` |  |
| `OS.get_static_memory_usage` |  |
| `OS.get_thread_caller_id` |  |
| `Object.get_instance_id` |  |
| `OpenXRAPIExtension.action_get_handle` |  |
| `OpenXRAPIExtension.get_hand_tracker` |  |
| `OpenXRAPIExtension.get_instance` |  |
| `OpenXRAPIExtension.get_instance_proc_addr` |  |
| `OpenXRAPIExtension.get_openxr_version` |  |
| `OpenXRAPIExtension.get_play_space` |  |
| `OpenXRAPIExtension.get_projection_layer` |  |
| `OpenXRAPIExtension.get_session` |  |
| `OpenXRAPIExtension.get_system_id` |  |
| `OpenXRAPIExtension.get_view_configuration` |  |
| `OpenXRAPIExtension.openxr_swapchain_create` |  |
| `OpenXRAPIExtension.openxr_swapchain_get_swapchain` |  |
| `OpenXRExtensionWrapper._get_composition_layer` |  |
| `OpenXRExtensionWrapper._set_android_surface_swapchain_create_info_and_get_next_pointer` |  |
| `OpenXRExtensionWrapper._set_frame_end_info_and_get_next_pointer` |  |
| `OpenXRExtensionWrapper._set_frame_wait_info_and_get_next_pointer` |  |
| `OpenXRExtensionWrapper._set_hand_joint_locations_and_get_next_pointer` |  |
| `OpenXRExtensionWrapper._set_instance_create_info_and_get_next_pointer` |  |
| `OpenXRExtensionWrapper._set_projection_layer_and_get_next_pointer` |  |
| `OpenXRExtensionWrapper._set_projection_views_and_get_next_pointer` |  |
| `OpenXRExtensionWrapper._set_reference_space_create_info_and_get_next_pointer` |  |
| `OpenXRExtensionWrapper._set_session_create_and_get_next_pointer` |  |
| `OpenXRExtensionWrapper._set_swapchain_create_info_and_get_next_pointer` |  |
| `OpenXRExtensionWrapper._set_system_properties_and_get_next_pointer` |  |
| `OpenXRExtensionWrapper._set_view_configuration_and_get_next_pointer` |  |
| `OpenXRExtensionWrapper._set_view_locate_info_and_get_next_pointer` |  |
| `OpenXRExtensionWrapper._set_viewport_composition_layer_and_get_next_pointer` |  |
| `OpenXRFutureResult.get_future` |  |
| `OpenXRSpatialAnchorCapability.get_persistence_context_handle` |  |
| `OpenXRSpatialCapabilityConfigurationBaseHeader._get_configuration` |  |
| `OpenXRSpatialCapabilityConfigurationBaseHeader.get_configuration` |  |
| `OpenXRSpatialComponentData._get_component_type` |  |
| `OpenXRSpatialComponentData._get_structure_data` |  |
| `OpenXRSpatialComponentPersistenceList.get_persistent_state` |  |
| `OpenXRSpatialEntityExtension.get_spatial_context_handle` |  |
| `OpenXRSpatialEntityExtension.get_spatial_entity_id` |  |
| `OpenXRSpatialEntityExtension.get_spatial_snapshot_handle` |  |
| `OpenXRSpatialQueryResultData.get_entity_id` |  |
| `OpenXRStructureBase._get_header` |  |
| `OpenXRStructureBase.get_structure_type` |  |
| `Performance.get_monitor_modification_time` |  |
| `PhysicsDirectBodyState2D.get_contact_collider_id` |  |
| `PhysicsDirectBodyState2DExtension._get_contact_collider_id` |  |
| `PhysicsDirectBodyState3D.get_contact_collider_id` |  |
| `PhysicsDirectBodyState3DExtension._get_contact_collider_id` |  |
| `PhysicsPointQueryParameters2D.get_canvas_instance_id` |  |
| `PhysicsServer2D.area_get_canvas_instance_id` |  |
| `PhysicsServer2D.area_get_object_instance_id` |  |
| `PhysicsServer2D.body_get_canvas_instance_id` |  |
| `PhysicsServer2D.body_get_object_instance_id` |  |
| `PhysicsServer2DExtension._area_get_canvas_instance_id` |  |
| `PhysicsServer2DExtension._area_get_object_instance_id` |  |
| `PhysicsServer2DExtension._body_get_canvas_instance_id` |  |
| `PhysicsServer2DExtension._body_get_object_instance_id` |  |
| `PhysicsServer3D.area_get_object_instance_id` |  |
| `PhysicsServer3D.body_get_object_instance_id` |  |
| `PhysicsServer3DExtension._area_get_object_instance_id` |  |
| `PhysicsServer3DExtension._body_get_object_instance_id` |  |
| `PhysicsTestMotionResult2D.get_collider_id` |  |
| `PhysicsTestMotionResult3D.get_collider_id` |  |
| `RandomNumberGenerator.get_seed` |  |
| `RandomNumberGenerator.get_state` |  |
| `RenderingDevice.buffer_get_device_address` |  |
| `RenderingDevice.get_captured_timestamp_cpu_time` |  |
| `RenderingDevice.get_captured_timestamp_gpu_time` |  |
| `RenderingDevice.get_captured_timestamps_frame` |  |
| `RenderingDevice.get_device_allocation_count` |  |
| `RenderingDevice.get_device_allocs_by_object_type` |  |
| `RenderingDevice.get_device_memory_by_object_type` |  |
| `RenderingDevice.get_device_total_memory` |  |
| `RenderingDevice.get_driver_allocation_count` |  |
| `RenderingDevice.get_driver_allocs_by_object_type` |  |
| `RenderingDevice.get_driver_memory_by_object_type` |  |
| `RenderingDevice.get_driver_resource` |  |
| `RenderingDevice.get_driver_total_memory` |  |
| `RenderingDevice.get_memory_usage` |  |
| `RenderingDevice.get_tracked_object_type_count` |  |
| `RenderingDevice.limit_get` |  |
| `RenderingDevice.shader_get_vertex_input_attribute_mask` |  |
| `RenderingDevice.texture_get_native_handle` |  |
| `RenderingServer.get_rendering_info` |  |
| `RenderingServer.texture_get_native_handle` |  |
| `Skeleton3D.get_version` |  |
| `StreamPeer.get_u64` |  |
| `Time.get_ticks_msec` |  |
| `Time.get_ticks_usec` |  |
| `UndoRedo.get_version` |  |
| `XMLParser.get_node_offset` |  |

### 3.2 `int64`（132 个）

| 方法 | 静态 |
|---|---|
| `AStar2D.get_available_point_id` |  |
| `AStar2D.get_closest_point` |  |
| `AStar2D.get_point_capacity` |  |
| `AStar2D.get_point_count` |  |
| `AStar3D.get_available_point_id` |  |
| `AStar3D.get_closest_point` |  |
| `AStar3D.get_point_capacity` |  |
| `AStar3D.get_point_count` |  |
| `AudioEffectCapture.get_discarded_frames` |  |
| `AudioEffectCapture.get_pushed_frames` |  |
| `AudioStreamPlaybackPolyphonic.play_stream` |  |
| `ClassDB.class_get_integer_constant` |  |
| `DisplayServer.window_get_native_handle` |  |
| `EditorExportPlatform.ssh_run_on_remote_no_wait` |  |
| `FileAccess.get_size` | static |
| `Font.get_face_count` |  |
| `Font.get_palette_count` |  |
| `FontFile.get_extra_spacing` |  |
| `FontFile.get_face_index` |  |
| `FontVariation.get_palette_index` |  |
| `GLTFAccessor.get_byte_offset` |  |
| `GLTFAccessor.get_count` |  |
| `GLTFAccessor.get_sparse_count` |  |
| `GLTFAccessor.get_sparse_indices_byte_offset` |  |
| `GLTFAccessor.get_sparse_values_byte_offset` |  |
| `GLTFBufferView.get_byte_length` |  |
| `GLTFBufferView.get_byte_offset` |  |
| `GLTFBufferView.get_byte_stride` |  |
| `HTTPClient.get_response_body_length` |  |
| `Image.get_data_size` |  |
| `Image.get_mipmap_offset` |  |
| `InputEventFromWindow.get_window_id` |  |
| `OpenXRAPIExtension.get_next_frame_time` |  |
| `OpenXRAPIExtension.get_predicted_display_time` |  |
| `OpenXRHapticVibration.get_duration` |  |
| `OpenXRSpatialComponentData.get_component_type` |  |
| `OpenXRSpatialQueryResultData.get_capacity` |  |
| `RDAccelerationStructureInstance.get_hit_sbt_range` |  |
| `RandomNumberGenerator.rand_weighted` |  |
| `RenderingDevice.compute_list_begin` |  |
| `RenderingDevice.draw_list_begin` |  |
| `RenderingDevice.draw_list_begin_for_screen` |  |
| `RenderingDevice.draw_list_switch_to_next_pass` |  |
| `RenderingDevice.framebuffer_format_create` |  |
| `RenderingDevice.framebuffer_format_create_empty` |  |
| `RenderingDevice.framebuffer_format_create_multipass` |  |
| `RenderingDevice.framebuffer_get_format` |  |
| `RenderingDevice.hit_sbt_range_alloc` |  |
| `RenderingDevice.raytracing_list_begin` |  |
| `RenderingDevice.screen_get_framebuffer_format` |  |
| `RenderingDevice.vertex_format_create` |  |
| `ResourceFormatLoader._get_resource_uid` |  |
| `ResourceLoader.get_resource_uid` |  |
| `ResourceSaver.get_resource_id_for_path` |  |
| `ResourceUID.create_id` |  |
| `ResourceUID.create_id_for_path` |  |
| `ResourceUID.text_to_id` |  |
| `SceneTree.get_frame` |  |
| `StreamPeer.get_64` |  |
| `TextServer.font_get_char_from_glyph_index` |  |
| `TextServer.font_get_face_count` |  |
| `TextServer.font_get_face_index` |  |
| `TextServer.font_get_fixed_size` |  |
| `TextServer.font_get_glyph_index` |  |
| `TextServer.font_get_glyph_texture_idx` |  |
| `TextServer.font_get_msdf_pixel_range` |  |
| `TextServer.font_get_msdf_size` |  |
| `TextServer.font_get_palette_count` |  |
| `TextServer.font_get_spacing` |  |
| `TextServer.font_get_stretch` |  |
| `TextServer.font_get_texture_count` |  |
| `TextServer.font_get_used_palette` |  |
| `TextServer.font_get_weight` |  |
| `TextServer.get_features` |  |
| `TextServer.is_confusable` |  |
| `TextServer.name_to_tag` |  |
| `TextServer.shaped_get_run_count` |  |
| `TextServer.shaped_get_span_count` |  |
| `TextServer.shaped_text_closest_character_pos` |  |
| `TextServer.shaped_text_get_custom_ellipsis` |  |
| `TextServer.shaped_text_get_ellipsis_glyph_count` |  |
| `TextServer.shaped_text_get_ellipsis_pos` |  |
| `TextServer.shaped_text_get_glyph_count` |  |
| `TextServer.shaped_text_get_object_glyph` |  |
| `TextServer.shaped_text_get_spacing` |  |
| `TextServer.shaped_text_get_trim_pos` |  |
| `TextServer.shaped_text_hit_test_grapheme` |  |
| `TextServer.shaped_text_hit_test_position` |  |
| `TextServer.shaped_text_next_character_pos` |  |
| `TextServer.shaped_text_next_grapheme_pos` |  |
| `TextServer.shaped_text_prev_character_pos` |  |
| `TextServer.shaped_text_prev_grapheme_pos` |  |
| `TextServerExtension._font_get_char_from_glyph_index` |  |
| `TextServerExtension._font_get_face_count` |  |
| `TextServerExtension._font_get_face_index` |  |
| `TextServerExtension._font_get_fixed_size` |  |
| `TextServerExtension._font_get_glyph_index` |  |
| `TextServerExtension._font_get_glyph_texture_idx` |  |
| `TextServerExtension._font_get_msdf_pixel_range` |  |
| `TextServerExtension._font_get_msdf_size` |  |
| `TextServerExtension._font_get_palette_count` |  |
| `TextServerExtension._font_get_spacing` |  |
| `TextServerExtension._font_get_stretch` |  |
| `TextServerExtension._font_get_texture_count` |  |
| `TextServerExtension._font_get_used_palette` |  |
| `TextServerExtension._font_get_weight` |  |
| `TextServerExtension._get_features` |  |
| `TextServerExtension._is_confusable` |  |
| `TextServerExtension._name_to_tag` |  |
| `TextServerExtension._shaped_get_run_count` |  |
| `TextServerExtension._shaped_get_span_count` |  |
| `TextServerExtension._shaped_text_closest_character_pos` |  |
| `TextServerExtension._shaped_text_get_custom_ellipsis` |  |
| `TextServerExtension._shaped_text_get_dominant_direction_in_range` |  |
| `TextServerExtension._shaped_text_get_ellipsis_glyph_count` |  |
| `TextServerExtension._shaped_text_get_ellipsis_pos` |  |
| `TextServerExtension._shaped_text_get_glyph_count` |  |
| `TextServerExtension._shaped_text_get_object_glyph` |  |
| `TextServerExtension._shaped_text_get_spacing` |  |
| `TextServerExtension._shaped_text_get_trim_pos` |  |
| `TextServerExtension._shaped_text_hit_test_grapheme` |  |
| `TextServerExtension._shaped_text_hit_test_position` |  |
| `TextServerExtension._shaped_text_next_character_pos` |  |
| `TextServerExtension._shaped_text_next_grapheme_pos` |  |
| `TextServerExtension._shaped_text_prev_character_pos` |  |
| `TextServerExtension._shaped_text_prev_grapheme_pos` |  |
| `Time.get_unix_time_from_datetime_dict` |  |
| `Time.get_unix_time_from_datetime_string` |  |
| `WorkerThreadPool.add_group_task` |  |
| `WorkerThreadPool.add_task` |  |
| `WorkerThreadPool.get_caller_group_id` |  |
| `WorkerThreadPool.get_caller_task_id` |  |

## 4. 含 64 位参数的类方法（441 个参数位）

### 4.1 `uint64` 参数（75 个）

| 方法 | 参数 | 静态 |
|---|---|---|
| `AnimationMixer._post_process_key_value` | `object_id` |  |
| `EncodedObjectAsID.set_object_id` | `id` |  |
| `ExternalTexture.set_external_buffer_id` | `external_buffer_id` |  |
| `FileAccess.seek` | `position` |  |
| `FileAccess.store_64` | `value` |  |
| `ImporterMesh.add_surface` | `flags` |  |
| `MeshDataTool.commit_to_surface` | `compression_flags` |  |
| `NavigationServer2D.link_set_owner_id` | `owner_id` |  |
| `NavigationServer2D.region_set_owner_id` | `owner_id` |  |
| `NavigationServer3D.link_set_owner_id` | `owner_id` |  |
| `NavigationServer3D.region_set_owner_id` | `owner_id` |  |
| `OpenXRAPIExtension.get_error_string` | `result` |  |
| `OpenXRAPIExtension.openxr_swapchain_acquire` | `swapchain` |  |
| `OpenXRAPIExtension.openxr_swapchain_create` | `create_flags` |  |
| `OpenXRAPIExtension.openxr_swapchain_create` | `usage_flags` |  |
| `OpenXRAPIExtension.openxr_swapchain_free` | `swapchain` |  |
| `OpenXRAPIExtension.openxr_swapchain_get_image` | `swapchain` |  |
| `OpenXRAPIExtension.openxr_swapchain_get_swapchain` | `swapchain` |  |
| `OpenXRAPIExtension.openxr_swapchain_release` | `swapchain` |  |
| `OpenXRAPIExtension.set_object_name` | `object_handle` |  |
| `OpenXRAPIExtension.xr_result` | `result` |  |
| `OpenXRExtensionWrapper._get_requested_extensions` | `xr_version` |  |
| `OpenXRExtensionWrapper._on_instance_created` | `instance` |  |
| `OpenXRExtensionWrapper._on_session_created` | `session` |  |
| `OpenXRExtensionWrapper._set_instance_create_info_and_get_next_pointer` | `xr_version` |  |
| `OpenXRFutureExtension.cancel_future` | `future` |  |
| `OpenXRFutureExtension.register_future` | `future` |  |
| `OpenXRRenderModelExtension.render_model_create` | `render_model_id` |  |
| `OpenXRSpatialComponentData._get_structure_data` | `next` |  |
| `OpenXRSpatialEntityExtension.add_spatial_entity` | `entity` |  |
| `OpenXRSpatialEntityExtension.add_spatial_entity` | `entity_id` |  |
| `OpenXRSpatialEntityExtension.find_spatial_entity` | `entity_id` |  |
| `OpenXRSpatialEntityExtension.get_float_buffer` | `buffer_id` |  |
| `OpenXRSpatialEntityExtension.get_string` | `buffer_id` |  |
| `OpenXRSpatialEntityExtension.get_uint16_buffer` | `buffer_id` |  |
| `OpenXRSpatialEntityExtension.get_uint32_buffer` | `buffer_id` |  |
| `OpenXRSpatialEntityExtension.get_uint8_buffer` | `buffer_id` |  |
| `OpenXRSpatialEntityExtension.get_vector2_buffer` | `buffer_id` |  |
| `OpenXRSpatialEntityExtension.get_vector3_buffer` | `buffer_id` |  |
| `OpenXRSpatialEntityExtension.make_spatial_entity` | `entity_id` |  |
| `OpenXRStructureBase._get_header` | `next` |  |
| `PhysicsDirectSpaceState2DExtension._intersect_point` | `canvas_instance_id` |  |
| `PhysicsPointQueryParameters2D.set_canvas_instance_id` | `canvas_instance_id` |  |
| `PhysicsServer2D.area_attach_canvas_instance_id` | `id` |  |
| `PhysicsServer2D.area_attach_object_instance_id` | `id` |  |
| `PhysicsServer2D.body_attach_canvas_instance_id` | `id` |  |
| `PhysicsServer2D.body_attach_object_instance_id` | `id` |  |
| `PhysicsServer2DExtension._area_attach_canvas_instance_id` | `id` |  |
| `PhysicsServer2DExtension._area_attach_object_instance_id` | `id` |  |
| `PhysicsServer2DExtension._body_attach_canvas_instance_id` | `id` |  |
| `PhysicsServer2DExtension._body_attach_object_instance_id` | `id` |  |
| `PhysicsServer2DExtension.body_test_motion_is_excluding_object` | `object` |  |
| `PhysicsServer3D.area_attach_object_instance_id` | `id` |  |
| `PhysicsServer3D.body_attach_object_instance_id` | `id` |  |
| `PhysicsServer3DExtension._area_attach_object_instance_id` | `id` |  |
| `PhysicsServer3DExtension._body_attach_object_instance_id` | `id` |  |
| `PhysicsServer3DExtension.body_test_motion_is_excluding_object` | `object` |  |
| `RandomNumberGenerator.set_seed` | `seed` |  |
| `RandomNumberGenerator.set_state` | `state` |  |
| `RenderingDevice.get_driver_resource` | `index` |  |
| `RenderingDevice.texture_create_from_extension` | `depth` |  |
| `RenderingDevice.texture_create_from_extension` | `height` |  |
| `RenderingDevice.texture_create_from_extension` | `image` |  |
| `RenderingDevice.texture_create_from_extension` | `layers` |  |
| `RenderingDevice.texture_create_from_extension` | `mipmaps` |  |
| `RenderingDevice.texture_create_from_extension` | `width` |  |
| `RenderingServer.instance_attach_object_instance_id` | `id` |  |
| `RenderingServer.texture_create_from_native_handle` | `native_handle` |  |
| `StreamPeer.put_u64` | `value` |  |
| `SurfaceTool.commit` | `flags` |  |
| `TextServer.is_valid_letter` | `unicode` |  |
| `TextServerExtension._is_valid_letter` | `unicode` |  |
| `XMLParser.seek` | `position` |  |
| `ZIPPacker.add_directory` | `modified_time` |  |
| `ZIPPacker.start_file` | `modified_time` |  |

### 4.2 `int64` 参数（366 个）

| 方法 | 参数 | 静态 |
|---|---|---|
| `AStar2D._compute_cost` | `from_id` |  |
| `AStar2D._compute_cost` | `to_id` |  |
| `AStar2D._estimate_cost` | `end_id` |  |
| `AStar2D._estimate_cost` | `from_id` |  |
| `AStar2D._filter_neighbor` | `from_id` |  |
| `AStar2D._filter_neighbor` | `neighbor_id` |  |
| `AStar2D.add_point` | `id` |  |
| `AStar2D.are_points_connected` | `id` |  |
| `AStar2D.are_points_connected` | `to_id` |  |
| `AStar2D.connect_points` | `id` |  |
| `AStar2D.connect_points` | `to_id` |  |
| `AStar2D.disconnect_points` | `id` |  |
| `AStar2D.disconnect_points` | `to_id` |  |
| `AStar2D.get_id_path` | `from_id` |  |
| `AStar2D.get_id_path` | `to_id` |  |
| `AStar2D.get_point_connections` | `id` |  |
| `AStar2D.get_point_path` | `from_id` |  |
| `AStar2D.get_point_path` | `to_id` |  |
| `AStar2D.get_point_position` | `id` |  |
| `AStar2D.get_point_weight_scale` | `id` |  |
| `AStar2D.has_point` | `id` |  |
| `AStar2D.is_point_disabled` | `id` |  |
| `AStar2D.remove_point` | `id` |  |
| `AStar2D.reserve_space` | `num_nodes` |  |
| `AStar2D.set_point_disabled` | `id` |  |
| `AStar2D.set_point_position` | `id` |  |
| `AStar2D.set_point_weight_scale` | `id` |  |
| `AStar3D._compute_cost` | `from_id` |  |
| `AStar3D._compute_cost` | `to_id` |  |
| `AStar3D._estimate_cost` | `end_id` |  |
| `AStar3D._estimate_cost` | `from_id` |  |
| `AStar3D._filter_neighbor` | `from_id` |  |
| `AStar3D._filter_neighbor` | `neighbor_id` |  |
| `AStar3D.add_point` | `id` |  |
| `AStar3D.are_points_connected` | `id` |  |
| `AStar3D.are_points_connected` | `to_id` |  |
| `AStar3D.connect_points` | `id` |  |
| `AStar3D.connect_points` | `to_id` |  |
| `AStar3D.disconnect_points` | `id` |  |
| `AStar3D.disconnect_points` | `to_id` |  |
| `AStar3D.get_id_path` | `from_id` |  |
| `AStar3D.get_id_path` | `to_id` |  |
| `AStar3D.get_point_connections` | `id` |  |
| `AStar3D.get_point_path` | `from_id` |  |
| `AStar3D.get_point_path` | `to_id` |  |
| `AStar3D.get_point_position` | `id` |  |
| `AStar3D.get_point_weight_scale` | `id` |  |
| `AStar3D.has_point` | `id` |  |
| `AStar3D.is_point_disabled` | `id` |  |
| `AStar3D.remove_point` | `id` |  |
| `AStar3D.reserve_space` | `num_nodes` |  |
| `AStar3D.set_point_disabled` | `id` |  |
| `AStar3D.set_point_position` | `id` |  |
| `AStar3D.set_point_weight_scale` | `id` |  |
| `AudioStreamPlaybackPolyphonic.is_stream_playing` | `stream` |  |
| `AudioStreamPlaybackPolyphonic.set_stream_pitch_scale` | `stream` |  |
| `AudioStreamPlaybackPolyphonic.set_stream_volume` | `stream` |  |
| `AudioStreamPlaybackPolyphonic.stop_stream` | `stream` |  |
| `DisplayServer.enable_for_stealing_focus` | `process_id` |  |
| `DisplayServer.tts_speak` | `utterance_id` |  |
| `EditorVCSInterface.create_commit` | `offset_minutes` |  |
| `EditorVCSInterface.create_commit` | `unix_timestamp` |  |
| `FileAccess.get_buffer` | `length` |  |
| `FileAccess.resize` | `length` |  |
| `FileAccess.seek_end` | `position` |  |
| `Font.find_variation` | `palette_index` |  |
| `Font.get_palette_colors` | `index` |  |
| `Font.get_palette_name` | `index` |  |
| `FontFile.set_extra_spacing` | `value` |  |
| `FontFile.set_face_index` | `face_index` |  |
| `FontVariation.set_palette_index` | `palette_index` |  |
| `GLTFAccessor.set_byte_offset` | `byte_offset` |  |
| `GLTFAccessor.set_count` | `count` |  |
| `GLTFAccessor.set_sparse_count` | `sparse_count` |  |
| `GLTFAccessor.set_sparse_indices_byte_offset` | `sparse_indices_byte_offset` |  |
| `GLTFAccessor.set_sparse_values_byte_offset` | `sparse_values_byte_offset` |  |
| `GLTFBufferView.set_byte_length` | `byte_length` |  |
| `GLTFBufferView.set_byte_offset` | `byte_offset` |  |
| `GLTFBufferView.set_byte_stride` | `byte_stride` |  |
| `InputEventFromWindow.set_window_id` | `id` |  |
| `OS.read_buffer_from_stdin` | `buffer_size` |  |
| `OS.read_string_from_stdin` | `buffer_size` |  |
| `OpenXRAPIExtension.get_swapchain_format_name` | `swapchain_format` |  |
| `OpenXRAPIExtension.openxr_swapchain_create` | `swapchain_format` |  |
| `OpenXRAPIExtension.set_object_name` | `object_type` |  |
| `OpenXRHapticVibration.set_duration` | `duration` |  |
| `OpenXRSpatialComponentAnchorList.get_entity_pose` | `index` |  |
| `OpenXRSpatialComponentBounded2DList.get_center_pose` | `index` |  |
| `OpenXRSpatialComponentBounded2DList.get_size` | `index` |  |
| `OpenXRSpatialComponentBounded3DList.get_center_pose` | `index` |  |
| `OpenXRSpatialComponentBounded3DList.get_size` | `index` |  |
| `OpenXRSpatialComponentMarkerList.get_marker_data` | `index` |  |
| `OpenXRSpatialComponentMarkerList.get_marker_id` | `index` |  |
| `OpenXRSpatialComponentMarkerList.get_marker_type` | `index` |  |
| `OpenXRSpatialComponentMesh2DList.get_indices` | `index` |  |
| `OpenXRSpatialComponentMesh2DList.get_transform` | `index` |  |
| `OpenXRSpatialComponentMesh2DList.get_vertices` | `index` |  |
| `OpenXRSpatialComponentMesh3DList.get_mesh` | `index` |  |
| `OpenXRSpatialComponentMesh3DList.get_transform` | `index` |  |
| `OpenXRSpatialComponentParentList.get_parent` | `index` |  |
| `OpenXRSpatialComponentPersistenceList.get_persistent_state` | `index` |  |
| `OpenXRSpatialComponentPersistenceList.get_persistent_uuid` | `index` |  |
| `OpenXRSpatialComponentPlaneAlignmentList.get_plane_alignment` | `index` |  |
| `OpenXRSpatialComponentPlaneSemanticLabelList.get_plane_semantic_label` | `index` |  |
| `OpenXRSpatialComponentPolygon2DList.get_transform` | `index` |  |
| `OpenXRSpatialComponentPolygon2DList.get_vertices` | `index` |  |
| `OpenXRSpatialQueryResultData.get_entity_id` | `index` |  |
| `OpenXRSpatialQueryResultData.get_entity_state` | `index` |  |
| `RDAccelerationStructureInstance.set_hit_sbt_range` | `p_member` |  |
| `RenderingDevice.compute_list_add_barrier` | `compute_list` |  |
| `RenderingDevice.compute_list_bind_compute_pipeline` | `compute_list` |  |
| `RenderingDevice.compute_list_bind_uniform_set` | `compute_list` |  |
| `RenderingDevice.compute_list_dispatch` | `compute_list` |  |
| `RenderingDevice.compute_list_dispatch_indirect` | `compute_list` |  |
| `RenderingDevice.compute_list_set_push_constant` | `compute_list` |  |
| `RenderingDevice.draw_list_bind_index_array` | `draw_list` |  |
| `RenderingDevice.draw_list_bind_render_pipeline` | `draw_list` |  |
| `RenderingDevice.draw_list_bind_uniform_set` | `draw_list` |  |
| `RenderingDevice.draw_list_bind_vertex_array` | `draw_list` |  |
| `RenderingDevice.draw_list_bind_vertex_buffers_format` | `draw_list` |  |
| `RenderingDevice.draw_list_bind_vertex_buffers_format` | `vertex_format` |  |
| `RenderingDevice.draw_list_disable_scissor` | `draw_list` |  |
| `RenderingDevice.draw_list_draw` | `draw_list` |  |
| `RenderingDevice.draw_list_draw_indirect` | `draw_list` |  |
| `RenderingDevice.draw_list_enable_scissor` | `draw_list` |  |
| `RenderingDevice.draw_list_set_blend_constants` | `draw_list` |  |
| `RenderingDevice.draw_list_set_push_constant` | `draw_list` |  |
| `RenderingDevice.framebuffer_create` | `validate_with_format` |  |
| `RenderingDevice.framebuffer_create_empty` | `validate_with_format` |  |
| `RenderingDevice.framebuffer_create_multipass` | `validate_with_format` |  |
| `RenderingDevice.framebuffer_format_get_texture_samples` | `format` |  |
| `RenderingDevice.hit_sbt_range_free` | `range` |  |
| `RenderingDevice.hit_sbt_range_update` | `range` |  |
| `RenderingDevice.raytracing_list_bind_raytracing_pipeline` | `raytracing_list` |  |
| `RenderingDevice.raytracing_list_bind_uniform_set` | `raytracing_list` |  |
| `RenderingDevice.raytracing_list_set_push_constant` | `raytracing_list` |  |
| `RenderingDevice.raytracing_list_trace_rays` | `raytracing_list` |  |
| `RenderingDevice.render_pipeline_create` | `framebuffer_format` |  |
| `RenderingDevice.render_pipeline_create` | `vertex_format` |  |
| `RenderingDevice.vertex_array_create` | `vertex_format` |  |
| `ResourceFormatSaver._set_uid` | `uid` |  |
| `ResourceSaver.set_uid` | `uid` |  |
| `ResourceUID.add_id` | `id` |  |
| `ResourceUID.get_id_path` | `id` |  |
| `ResourceUID.has_id` | `id` |  |
| `ResourceUID.id_to_text` | `id` |  |
| `ResourceUID.remove_id` | `id` |  |
| `ResourceUID.set_id` | `id` |  |
| `StreamPeer.put_64` | `value` |  |
| `TextServer.draw_hex_code_box` | `index` |  |
| `TextServer.draw_hex_code_box` | `size` |  |
| `TextServer.font_clear_kerning_map` | `size` |  |
| `TextServer.font_draw_glyph` | `index` |  |
| `TextServer.font_draw_glyph` | `size` |  |
| `TextServer.font_draw_glyph_outline` | `index` |  |
| `TextServer.font_draw_glyph_outline` | `outline_size` |  |
| `TextServer.font_draw_glyph_outline` | `size` |  |
| `TextServer.font_get_ascent` | `size` |  |
| `TextServer.font_get_char_from_glyph_index` | `glyph_index` |  |
| `TextServer.font_get_char_from_glyph_index` | `size` |  |
| `TextServer.font_get_descent` | `size` |  |
| `TextServer.font_get_glyph_advance` | `glyph` |  |
| `TextServer.font_get_glyph_advance` | `size` |  |
| `TextServer.font_get_glyph_contours` | `index` |  |
| `TextServer.font_get_glyph_contours` | `size` |  |
| `TextServer.font_get_glyph_index` | `char` |  |
| `TextServer.font_get_glyph_index` | `size` |  |
| `TextServer.font_get_glyph_index` | `variation_selector` |  |
| `TextServer.font_get_glyph_offset` | `glyph` |  |
| `TextServer.font_get_glyph_size` | `glyph` |  |
| `TextServer.font_get_glyph_texture_idx` | `glyph` |  |
| `TextServer.font_get_glyph_texture_rid` | `glyph` |  |
| `TextServer.font_get_glyph_texture_size` | `glyph` |  |
| `TextServer.font_get_glyph_uv_rect` | `glyph` |  |
| `TextServer.font_get_kerning` | `size` |  |
| `TextServer.font_get_kerning_list` | `size` |  |
| `TextServer.font_get_palette_colors` | `index` |  |
| `TextServer.font_get_palette_name` | `index` |  |
| `TextServer.font_get_scale` | `size` |  |
| `TextServer.font_get_texture_image` | `texture_index` |  |
| `TextServer.font_get_texture_offsets` | `texture_index` |  |
| `TextServer.font_get_underline_position` | `size` |  |
| `TextServer.font_get_underline_thickness` | `size` |  |
| `TextServer.font_has_char` | `char` |  |
| `TextServer.font_remove_glyph` | `glyph` |  |
| `TextServer.font_remove_kerning` | `size` |  |
| `TextServer.font_remove_texture` | `texture_index` |  |
| `TextServer.font_render_glyph` | `index` |  |
| `TextServer.font_render_range` | `end` |  |
| `TextServer.font_render_range` | `start` |  |
| `TextServer.font_set_ascent` | `size` |  |
| `TextServer.font_set_descent` | `size` |  |
| `TextServer.font_set_face_index` | `face_index` |  |
| `TextServer.font_set_fixed_size` | `fixed_size` |  |
| `TextServer.font_set_glyph_advance` | `glyph` |  |
| `TextServer.font_set_glyph_advance` | `size` |  |
| `TextServer.font_set_glyph_offset` | `glyph` |  |
| `TextServer.font_set_glyph_size` | `glyph` |  |
| `TextServer.font_set_glyph_texture_idx` | `glyph` |  |
| `TextServer.font_set_glyph_texture_idx` | `texture_idx` |  |
| `TextServer.font_set_glyph_uv_rect` | `glyph` |  |
| `TextServer.font_set_kerning` | `size` |  |
| `TextServer.font_set_msdf_pixel_range` | `msdf_pixel_range` |  |
| `TextServer.font_set_msdf_size` | `msdf_size` |  |
| `TextServer.font_set_scale` | `size` |  |
| `TextServer.font_set_spacing` | `value` |  |
| `TextServer.font_set_stretch` | `weight` |  |
| `TextServer.font_set_texture_image` | `texture_index` |  |
| `TextServer.font_set_texture_offsets` | `texture_index` |  |
| `TextServer.font_set_underline_position` | `size` |  |
| `TextServer.font_set_underline_thickness` | `size` |  |
| `TextServer.font_set_used_palette` | `index` |  |
| `TextServer.font_set_weight` | `weight` |  |
| `TextServer.get_hex_code_box_size` | `index` |  |
| `TextServer.get_hex_code_box_size` | `size` |  |
| `TextServer.shaped_get_run_direction` | `index` |  |
| `TextServer.shaped_get_run_font_rid` | `index` |  |
| `TextServer.shaped_get_run_font_size` | `index` |  |
| `TextServer.shaped_get_run_glyph_range` | `index` |  |
| `TextServer.shaped_get_run_language` | `index` |  |
| `TextServer.shaped_get_run_object` | `index` |  |
| `TextServer.shaped_get_run_range` | `index` |  |
| `TextServer.shaped_get_run_text` | `index` |  |
| `TextServer.shaped_get_span_embedded_object` | `index` |  |
| `TextServer.shaped_get_span_meta` | `index` |  |
| `TextServer.shaped_get_span_object` | `index` |  |
| `TextServer.shaped_get_span_text` | `index` |  |
| `TextServer.shaped_set_span_update_font` | `index` |  |
| `TextServer.shaped_set_span_update_font` | `size` |  |
| `TextServer.shaped_text_add_object` | `length` |  |
| `TextServer.shaped_text_add_string` | `size` |  |
| `TextServer.shaped_text_closest_character_pos` | `pos` |  |
| `TextServer.shaped_text_draw_outline` | `outline_size` |  |
| `TextServer.shaped_text_get_carets` | `position` |  |
| `TextServer.shaped_text_get_dominant_direction_in_range` | `end` |  |
| `TextServer.shaped_text_get_dominant_direction_in_range` | `start` |  |
| `TextServer.shaped_text_get_grapheme_bounds` | `pos` |  |
| `TextServer.shaped_text_get_line_breaks` | `start` |  |
| `TextServer.shaped_text_get_line_breaks_adv` | `start` |  |
| `TextServer.shaped_text_get_selection` | `end` |  |
| `TextServer.shaped_text_get_selection` | `start` |  |
| `TextServer.shaped_text_next_character_pos` | `pos` |  |
| `TextServer.shaped_text_next_grapheme_pos` | `pos` |  |
| `TextServer.shaped_text_prev_character_pos` | `pos` |  |
| `TextServer.shaped_text_prev_grapheme_pos` | `pos` |  |
| `TextServer.shaped_text_set_custom_ellipsis` | `char` |  |
| `TextServer.shaped_text_set_spacing` | `value` |  |
| `TextServer.shaped_text_substr` | `length` |  |
| `TextServer.shaped_text_substr` | `start` |  |
| `TextServer.string_get_word_breaks` | `chars_per_line` |  |
| `TextServer.tag_to_name` | `tag` |  |
| `TextServerExtension._draw_hex_code_box` | `index` |  |
| `TextServerExtension._draw_hex_code_box` | `size` |  |
| `TextServerExtension._font_clear_kerning_map` | `size` |  |
| `TextServerExtension._font_draw_glyph` | `index` |  |
| `TextServerExtension._font_draw_glyph` | `size` |  |
| `TextServerExtension._font_draw_glyph_outline` | `index` |  |
| `TextServerExtension._font_draw_glyph_outline` | `outline_size` |  |
| `TextServerExtension._font_draw_glyph_outline` | `size` |  |
| `TextServerExtension._font_get_ascent` | `size` |  |
| `TextServerExtension._font_get_char_from_glyph_index` | `glyph_index` |  |
| `TextServerExtension._font_get_char_from_glyph_index` | `size` |  |
| `TextServerExtension._font_get_descent` | `size` |  |
| `TextServerExtension._font_get_glyph_advance` | `glyph` |  |
| `TextServerExtension._font_get_glyph_advance` | `size` |  |
| `TextServerExtension._font_get_glyph_contours` | `index` |  |
| `TextServerExtension._font_get_glyph_contours` | `size` |  |
| `TextServerExtension._font_get_glyph_index` | `char` |  |
| `TextServerExtension._font_get_glyph_index` | `size` |  |
| `TextServerExtension._font_get_glyph_index` | `variation_selector` |  |
| `TextServerExtension._font_get_glyph_offset` | `glyph` |  |
| `TextServerExtension._font_get_glyph_size` | `glyph` |  |
| `TextServerExtension._font_get_glyph_texture_idx` | `glyph` |  |
| `TextServerExtension._font_get_glyph_texture_rid` | `glyph` |  |
| `TextServerExtension._font_get_glyph_texture_size` | `glyph` |  |
| `TextServerExtension._font_get_glyph_uv_rect` | `glyph` |  |
| `TextServerExtension._font_get_kerning` | `size` |  |
| `TextServerExtension._font_get_kerning_list` | `size` |  |
| `TextServerExtension._font_get_palette_colors` | `index` |  |
| `TextServerExtension._font_get_palette_name` | `index` |  |
| `TextServerExtension._font_get_scale` | `size` |  |
| `TextServerExtension._font_get_texture_image` | `texture_index` |  |
| `TextServerExtension._font_get_texture_offsets` | `texture_index` |  |
| `TextServerExtension._font_get_underline_position` | `size` |  |
| `TextServerExtension._font_get_underline_thickness` | `size` |  |
| `TextServerExtension._font_has_char` | `char` |  |
| `TextServerExtension._font_remove_glyph` | `glyph` |  |
| `TextServerExtension._font_remove_kerning` | `size` |  |
| `TextServerExtension._font_remove_texture` | `texture_index` |  |
| `TextServerExtension._font_render_glyph` | `index` |  |
| `TextServerExtension._font_render_range` | `end` |  |
| `TextServerExtension._font_render_range` | `start` |  |
| `TextServerExtension._font_set_ascent` | `size` |  |
| `TextServerExtension._font_set_data_ptr` | `data_size` |  |
| `TextServerExtension._font_set_descent` | `size` |  |
| `TextServerExtension._font_set_face_index` | `face_index` |  |
| `TextServerExtension._font_set_fixed_size` | `fixed_size` |  |
| `TextServerExtension._font_set_glyph_advance` | `glyph` |  |
| `TextServerExtension._font_set_glyph_advance` | `size` |  |
| `TextServerExtension._font_set_glyph_offset` | `glyph` |  |
| `TextServerExtension._font_set_glyph_size` | `glyph` |  |
| `TextServerExtension._font_set_glyph_texture_idx` | `glyph` |  |
| `TextServerExtension._font_set_glyph_texture_idx` | `texture_idx` |  |
| `TextServerExtension._font_set_glyph_uv_rect` | `glyph` |  |
| `TextServerExtension._font_set_kerning` | `size` |  |
| `TextServerExtension._font_set_msdf_pixel_range` | `msdf_pixel_range` |  |
| `TextServerExtension._font_set_msdf_size` | `msdf_size` |  |
| `TextServerExtension._font_set_scale` | `size` |  |
| `TextServerExtension._font_set_spacing` | `value` |  |
| `TextServerExtension._font_set_stretch` | `stretch` |  |
| `TextServerExtension._font_set_texture_image` | `texture_index` |  |
| `TextServerExtension._font_set_texture_offsets` | `texture_index` |  |
| `TextServerExtension._font_set_underline_position` | `size` |  |
| `TextServerExtension._font_set_underline_thickness` | `size` |  |
| `TextServerExtension._font_set_used_palette` | `index` |  |
| `TextServerExtension._font_set_weight` | `weight` |  |
| `TextServerExtension._get_hex_code_box_size` | `index` |  |
| `TextServerExtension._get_hex_code_box_size` | `size` |  |
| `TextServerExtension._shaped_get_run_direction` | `index` |  |
| `TextServerExtension._shaped_get_run_font_rid` | `index` |  |
| `TextServerExtension._shaped_get_run_font_size` | `index` |  |
| `TextServerExtension._shaped_get_run_glyph_range` | `index` |  |
| `TextServerExtension._shaped_get_run_language` | `index` |  |
| `TextServerExtension._shaped_get_run_object` | `index` |  |
| `TextServerExtension._shaped_get_run_range` | `index` |  |
| `TextServerExtension._shaped_get_run_text` | `index` |  |
| `TextServerExtension._shaped_get_span_embedded_object` | `index` |  |
| `TextServerExtension._shaped_get_span_meta` | `index` |  |
| `TextServerExtension._shaped_get_span_object` | `index` |  |
| `TextServerExtension._shaped_get_span_text` | `index` |  |
| `TextServerExtension._shaped_set_span_update_font` | `index` |  |
| `TextServerExtension._shaped_set_span_update_font` | `size` |  |
| `TextServerExtension._shaped_text_add_object` | `length` |  |
| `TextServerExtension._shaped_text_add_string` | `size` |  |
| `TextServerExtension._shaped_text_closest_character_pos` | `pos` |  |
| `TextServerExtension._shaped_text_draw_outline` | `outline_size` |  |
| `TextServerExtension._shaped_text_get_carets` | `position` |  |
| `TextServerExtension._shaped_text_get_dominant_direction_in_range` | `end` |  |
| `TextServerExtension._shaped_text_get_dominant_direction_in_range` | `start` |  |
| `TextServerExtension._shaped_text_get_grapheme_bounds` | `pos` |  |
| `TextServerExtension._shaped_text_get_line_breaks` | `start` |  |
| `TextServerExtension._shaped_text_get_line_breaks_adv` | `start` |  |
| `TextServerExtension._shaped_text_get_selection` | `end` |  |
| `TextServerExtension._shaped_text_get_selection` | `start` |  |
| `TextServerExtension._shaped_text_next_character_pos` | `pos` |  |
| `TextServerExtension._shaped_text_next_grapheme_pos` | `pos` |  |
| `TextServerExtension._shaped_text_prev_character_pos` | `pos` |  |
| `TextServerExtension._shaped_text_prev_grapheme_pos` | `pos` |  |
| `TextServerExtension._shaped_text_set_custom_ellipsis` | `char` |  |
| `TextServerExtension._shaped_text_set_spacing` | `value` |  |
| `TextServerExtension._shaped_text_substr` | `length` |  |
| `TextServerExtension._shaped_text_substr` | `start` |  |
| `TextServerExtension._string_get_word_breaks` | `chars_per_line` |  |
| `TextServerExtension._tag_to_name` | `tag` |  |
| `Time.get_date_dict_from_unix_time` | `unix_time_val` |  |
| `Time.get_date_string_from_unix_time` | `unix_time_val` |  |
| `Time.get_datetime_dict_from_unix_time` | `unix_time_val` |  |
| `Time.get_datetime_string_from_unix_time` | `unix_time_val` |  |
| `Time.get_offset_string_from_offset_minutes` | `offset_minutes` |  |
| `Time.get_time_dict_from_unix_time` | `unix_time_val` |  |
| `Time.get_time_string_from_unix_time` | `unix_time_val` |  |
| `WorkerThreadPool.get_group_processed_element_count` | `group_id` |  |
| `WorkerThreadPool.is_group_task_completed` | `group_id` |  |
| `WorkerThreadPool.is_task_completed` | `task_id` |  |
| `WorkerThreadPool.wait_for_group_task_completion` | `group_id` |  |
| `WorkerThreadPool.wait_for_task_completion` | `task_id` |  |
