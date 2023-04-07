#include <stdio.h>
#include <time.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>
#include <cglm/cglm.h>

#include <stdint.h>

#include <bs_mem.h>
#include <bs_core.h>
#include <bs_math.h>
#include <bs_models.h>
#include <bs_shaders.h>
#include <bs_textures.h>
#include <bs_debug.h>

bs_Joint identity_joint = { GLM_MAT4_IDENTITY_INIT };

typedef struct {
    int frame;
    bs_mat4 joints[];
} SSBO_Data;

bs_Anim *anims = NULL;
int anim_count = 0;
int allocated_anims = 0;

int64_t curr_tex_ptr = 0;
bs_I32 anim_ssbo = -1;
bs_U32 anim_offset = 0;
int attrib_offset = 0;

int load_settings = 0;

/* --- VERTEX PARSING --- */
void bs_modelAttribData(int accessor_idx, int offset, bs_Prim *prim, cgltf_data *data) {
    cgltf_accessor *accessor = data->accessors + accessor_idx;
    int num_floats = cgltf_accessor_unpack_floats(accessor, NULL, 0);
    int num_comps = cgltf_num_components(accessor->type);

    for(int i = 0; i < num_floats / num_comps; i++, offset += prim->vertex_size) {
	cgltf_accessor_read_float(accessor, i, (float *)prim->vertices + offset, num_comps);
    }
}

void bs_modelAttribDataI(int accessor_idx, int offset, bs_Prim *prim, cgltf_data *data) {
    cgltf_accessor *accessor = data->accessors + accessor_idx;
    int num_floats = cgltf_accessor_unpack_floats(accessor, NULL, 0);
    int num_comps = cgltf_num_components(accessor->type);

    for(int i = 0; i < num_floats / num_comps; i++, offset += prim->vertex_size) {
	cgltf_accessor_read_uint(accessor, i, (unsigned int *)prim->vertices + offset, num_comps);
    }
}

void bs_loadMaterial(bs_Model *model, cgltf_data *c_data, cgltf_primitive *c_prim, bs_Prim *prim) {
    cgltf_material *c_mat = c_prim->material;
    bs_Material *mat = &prim->material;

    if(c_mat == NULL) {
	mat->col = BS_WHITE;
	mat->tex_idx = -1;
	mat->metallic = 0.0;
	return;
    }

    cgltf_pbr_metallic_roughness *metallic = &c_mat->pbr_metallic_roughness;
    cgltf_float *mat_color = metallic->base_color_factor;

    mat->col.r = mat_color[0] * 255;
    mat->col.g = mat_color[1] * 255;
    mat->col.b = mat_color[2] * 255;
    mat->col.a = mat_color[3] * 255;

    // If the primitive has a texture
    cgltf_texture *tex = metallic->base_color_texture.texture;
    if(tex != NULL) {
	int id = (int64_t)tex->image;
	id -= (int64_t)c_data->textures[0].image;
	id /= sizeof(cgltf_image);
	printf("TEXTURE ID: %d\n", id);

	mat->tex_idx = id;
    }

    mat->metallic = metallic->metallic_factor;
}

bool bs_checkWindingConflict(bs_Prim *prim, int num_tris, int cur, int idx0, int idx1) {
    int *vertex = (int *)prim->vertices + prim->offset_idx;

    for(int i = 0; i < num_tris; i++) {
	if(i == cur)
	    continue;

	int first_idx = 6 * i;
	int v0, v1, v2;

	v0 = *(vertex + (prim->vertex_size * prim->indices[first_idx + 0]));
	v1 = *(vertex + (prim->vertex_size * prim->indices[first_idx + 2]));
	v2 = *(vertex + (prim->vertex_size * prim->indices[first_idx + 4]));
	
	if((v0 == idx0 && v1 == idx1))
	    return true;

	if(v1 == idx0 && v2 == idx1)
	    return true;

	if(v2 == idx0 && v0 == idx1)
	    return true;

    }
    return false;
}

int bs_findAdjacentIndex(int num_tris, bs_Prim *prim, int idx1, int idx2, int idx3) {
    int *vertex = (int *)prim->vertices + prim->offset_idx;
    
    for(int i = 0; i < num_tris; i++) {
	int first_idx = 6 * i;
	int face_indices[3];
    
	for(int j = 0; j < 3; j++) {
	    int idx = prim->indices[first_idx + j * 2];

	    int *idx_p = vertex + (idx * prim->vertex_size);
	    face_indices[j] = *idx_p;
	}

	for(int edge = 0; edge < 3; edge++) {
	    int v1 = face_indices[edge];
	    int v2 = face_indices[(edge + 1) % 3];
	    int opp = face_indices[(edge + 2) % 3];

	    /* If the two triangles share an edge */
	    if(((v1 == idx1 && v2 == idx2) || (v2 == idx1 && v1 == idx2)) && opp != idx3)
		return opp;
	}
    }

    /* Return the opposite vertex if no adjacent was found */
    return idx3;
}

/* TODO: This is currently broken */
void bs_readIndicesAdjacent(bs_Mesh *mesh, bs_Model *model, cgltf_mesh *c_mesh, int prim_index) {
    bs_Prim *prim = mesh->prims + prim_index;

    // Read indices
    int num_indices = cgltf_accessor_unpack_floats(c_mesh->primitives[prim_index].indices, NULL, 0);
    int num_adjacent = num_indices * 2;
    prim->indices = malloc(num_adjacent * sizeof(int));
    prim->index_count = num_adjacent;
    for(int i = 0; i < num_indices; i++) {
	cgltf_uint outv;
	cgltf_accessor_read_uint(c_mesh->primitives[prim_index].indices, i, &outv, 1);
	prim->indices[i * 2] = outv;
    }

    attrib_offset = c_mesh->primitives[prim_index].index_id + 1;

    mesh->vertex_count += prim->vertex_count;
    mesh->index_count += prim->index_count;
    model->vertex_count += prim->vertex_count;
    model->index_count += num_adjacent;
}

void bs_readIndices(bs_Mesh *mesh, bs_Model *model, cgltf_mesh *c_mesh, int prim_index) {
    bs_Prim *prim = mesh->prims + prim_index;

    // Read indices
    int num_indices = cgltf_accessor_unpack_floats(c_mesh->primitives[prim_index].indices, NULL, 0);
    prim->indices = malloc(num_indices * sizeof(int));
    prim->index_count = num_indices;
    for(int i = 0; i < num_indices; i++) {
	cgltf_uint outv;
	cgltf_accessor_read_uint(c_mesh->primitives[prim_index].indices, i, &outv, 1);
	prim->indices[i] = outv;
    }

    attrib_offset = c_mesh->primitives[prim_index].index_id + 1;

    mesh->vertex_count += prim->vertex_count;
    mesh->index_count += prim->index_count;
    model->vertex_count += prim->vertex_count;
    model->index_count += num_indices;
}

void bs_loadPrim(cgltf_data *data, bs_Mesh *mesh, bs_Model *model, int mesh_index, int prim_index) {
    cgltf_mesh *c_mesh = data->meshes + mesh_index;
    cgltf_primitive *c_prim = c_mesh->primitives + prim_index;
    bs_Prim *prim = &mesh->prims[prim_index];

    int attrib_count = c_mesh->primitives[prim_index].attributes_count;
    int num_floats = cgltf_accessor_unpack_floats(&data->accessors[c_mesh->primitives[prim_index].attributes[0].index], NULL, 0);

    int vertex_size = 0;
    bs_loadMaterial(model, data, &c_mesh->primitives[prim_index], prim);

    prim->offset_nor = 0;
    prim->offset_tex = 0;
    prim->offset_bid = 0;
    prim->offset_wei = 0;
    prim->offset_tex = 0;

    // Calculate vertex size
    for(int i = 0; i < attrib_count; i++) {
    	int index = c_mesh->primitives[prim_index].attributes[i].index;
    	int type = c_mesh->primitives[prim_index].attributes[i].type;

	// Position has no offset since it is always at offset 0 
    	switch(type) {
	    case cgltf_attribute_type_position : vertex_size += 3; break;
	    case cgltf_attribute_type_normal   : prim->offset_nor = vertex_size; vertex_size += 3; break;
	    case cgltf_attribute_type_texcoord : prim->offset_tex = vertex_size; vertex_size += 2; break;
	    case cgltf_attribute_type_joints   : prim->offset_bid = vertex_size; vertex_size += 4; break;
	    case cgltf_attribute_type_weights  : prim->offset_wei = vertex_size; vertex_size += 4; break;
    	}
    }

    prim->vertex_size = vertex_size;
    prim->vertex_count = num_floats / 3;
    prim->vertices = malloc(prim->vertex_count * vertex_size  * sizeof(float));

    // Read vertices
    for(int i = 0; i < attrib_count; i++) {
    	int index = c_mesh->primitives[prim_index].attributes[i].index;
    	int type = c_mesh->primitives[prim_index].attributes[i].type;

    	switch(type) {
	    case cgltf_attribute_type_position : bs_modelAttribData (index, 0, prim, data); break;
	    case cgltf_attribute_type_normal   : bs_modelAttribData (index, prim->offset_nor, prim, data); break;
	    case cgltf_attribute_type_texcoord : bs_modelAttribData (index, prim->offset_tex, prim, data); break;
	    case cgltf_attribute_type_joints   : bs_modelAttribDataI(index, prim->offset_bid, prim, data); break;
	    case cgltf_attribute_type_weights  : bs_modelAttribData (index, prim->offset_wei, prim, data); break;
    	}
    }

    bs_readIndices(mesh, model, c_mesh, prim_index);
}

void bs_loadMesh(cgltf_data *data, bs_Model *model, int mesh_index) {
    cgltf_mesh *c_mesh = &data->meshes[mesh_index];
    cgltf_node *node = &data->nodes[mesh_index];

    bs_Mesh *mesh = model->meshes + mesh_index;

    int c_mesh_name_len = strlen(c_mesh->node->name);
    mesh->id = mesh_index;
    mesh->index_count = 0;
    mesh->vertex_count = 0;
    mesh->name = malloc(c_mesh_name_len+1);
    int n = sprintf(mesh->name, "%s", c_mesh->node->name);

    memcpy(&mesh->pos, node->translation, sizeof(bs_vec3));
    memcpy(&mesh->rot, node->rotation, sizeof(bs_vec4));
    memcpy(&mesh->sca, node->scale, sizeof(bs_vec4));
   
    bs_mat4 local = GLM_MAT4_IDENTITY_INIT;
    glm_translate(local, node->translation);
    glm_quat_rotate(local, node->rotation, local);
    glm_scale(local, node->scale);
 
    memcpy(mesh->mat, &local, sizeof(bs_mat4));

    mesh->prims = malloc(c_mesh->primitives_count * sizeof(bs_Prim));
    mesh->prim_count = c_mesh->primitives_count;

    for(int i = 0; i < c_mesh->primitives_count; i++)
	bs_loadPrim(data, mesh, model, mesh_index, i);
}

void bs_checkMeshAnimResize(bs_Anim *anim) {
    const int realloc_by = 8;
    if((anim->num_mesh_anims % realloc_by) != 0)
	return;

    size_t size = (realloc_by + anim->num_mesh_anims) * sizeof(bs_MeshAnim);
    anim->mesh_anims = realloc(anim->mesh_anims, size);
}

void bs_jointlessAnimation(bs_Anim *anim, bs_Mesh *mesh) {
    bs_checkMeshAnimResize(anim);
    bs_MeshAnim *mesh_anim = anim->mesh_anims + anim->num_mesh_anims;

    mesh_anim->joints = malloc(anim->frame_count * sizeof(bs_mat4));
    mesh_anim->num_joints = anim->joint_count;
    mesh_anim->num_frames = anim->frame_count;
    mesh_anim->shader_offset = anim_offset;
    anim_offset += anim->frame_count;

    // For each FRAME in animation
    for(int i = 0; i < anim->frame_count; i++)
	memcpy(mesh_anim->joints[i], anim->matrices[i], sizeof(bs_mat4));

    anim->num_mesh_anims++;
}

void bs_animation(bs_Anim *anim, bs_Skin *skin) {
    if(anim->joint_count != skin->joint_count) {
	//bs_jointlessAnimation(anim, mesh);
	printf("Mismatching number of joints in animation and mesh\n");
	return;
    }

    bs_checkMeshAnimResize(anim);
    bs_MeshAnim *mesh_anim = anim->mesh_anims + anim->num_mesh_anims;

    mesh_anim->joints = malloc(anim->frame_count * anim->joint_count * sizeof(bs_mat4));
    mesh_anim->num_joints = anim->joint_count;
    mesh_anim->num_frames = anim->frame_count;
    mesh_anim->shader_offset = anim_offset;
    anim_offset += anim->frame_count * anim->joint_count;

    // For each FRAME in animation
    for(int i = 0; i < anim->frame_count; i++) {
	// For each JOINT in mesh
	for(int j = 0; j < anim->joint_count; j++) {
	    bs_Joint *change_joint = &skin->joints[j];
	    bs_Joint *parent = skin->joints[j].parent;
	    int idx = j + i * anim->joint_count;

	    // RESULT_JOINT  = (BIND MATRIX) * (LOCAL INVERSE)
	    // RESULT_JOINT *= (ANIMATION JOINT OF CURRENT FRAME)
	    // RESULT_JOINT *= (INVERSE BIND MATRIX)
	    // RESULT_JOINT  = (JOINT PARENT) * (RESULT_JOINT)
	    
	    glm_mat4_mul(change_joint->bind_matrix, change_joint->local_inv, change_joint->mat);
	    glm_mat4_mul(change_joint->mat, anim->matrices[idx], change_joint->mat);
	    glm_mat4_mul(change_joint->mat, change_joint->bind_matrix_inv, change_joint->mat);
	    glm_mat4_mul(parent->mat, change_joint->mat, change_joint->mat);
	    
	    memcpy(mesh_anim->joints + idx, change_joint->mat, sizeof(bs_mat4));
/*
	    bs_mat4 res;
	    glm_mat4_mul(anim->matrices[idx], mesh->mat, res);
	    memcpy(mesh_anim->joints + idx, res, sizeof(bs_mat4));*/
	}
    }

    anim->num_mesh_anims++;
}

void bs_loadAnim(cgltf_data* data, int index, int old_anim_offset, bs_Model *model) {
    cgltf_animation *c_anim = &data->animations[index];
    bs_Anim *anim = &anims[index + old_anim_offset];

    int joint_count = c_anim->samplers_count / 3;
    int frame_count = cgltf_accessor_unpack_floats(c_anim->samplers[0].input, NULL, 0);

    int name_len = strlen(c_anim->name);

    bs_mat4 *joints = malloc(joint_count * frame_count * sizeof(bs_mat4));
    anim->name = malloc(name_len + 1);
    strcpy(anim->name, c_anim->name);

    anim->mesh_anims = NULL;
    anim->num_mesh_anims = 0;
    
    anim->joint_count = joint_count;
    anim->frame_count = frame_count;

    int i, i3 = 0; 
    for(i = 0; i < joint_count; i++, i3+=3) {
	cgltf_animation_sampler *c_sampler = &c_anim->samplers[i3];
	cgltf_animation_channel *c_channel = &c_anim->channels[i3];

	// Input accessor contains timings for every frame in current joint
	// Output contains translation, rotation and scale for every frame in current joint
	cgltf_accessor *input = c_sampler->input;
	cgltf_accessor *translation_output = c_sampler->output + 0;
	cgltf_accessor *rotation_output    = c_sampler->output + 1;
	cgltf_accessor *scale_output       = c_sampler->output + 2; 

	for(int j = 0; j < frame_count; j++) {
	    int idx = i + (j * joint_count);
	    bs_mat4 *joint = &joints[idx];
	    bs_mat4 joint_mat = GLM_MAT4_IDENTITY_INIT;

	    vec3   tra;
	    versor rot;
	    vec3   sca;

	    cgltf_accessor_read_float(translation_output, j, (float*)&tra, 3);
	    cgltf_accessor_read_float(rotation_output   , j, (float*)&rot, 4);
	    cgltf_accessor_read_float(scale_output      , j, (float*)&sca, 3);

	    glm_translate(joint_mat, tra);
	    glm_quat_rotate(joint_mat, rot, joint_mat);
	    glm_scale(joint_mat, sca);

	    memcpy(joint, joint_mat, sizeof(bs_mat4));
	}
    }

    for(int j = 0; j < model->mesh_count; j++) {
	bs_Mesh *mesh = model->meshes + j;
	cgltf_mesh *c_mesh = &data->meshes[j];
	if(c_anim->channels->target_node == c_mesh->node)
	    anim->mesh = mesh;
    }

    anim->matrices = joints;
}

void bs_loadAnims(cgltf_data* data, bs_Model *model) {
    int old_anim_count = anim_count;
    anim_count += data->animations_count;

    if(data->animations_count == 0)
	return;
    
    model->anim_offset = old_anim_count;
    model->anim_count = data->animations_count;

    if(anim_count > allocated_anims) {
	allocated_anims = anim_count + 8;
	anims = realloc(anims, allocated_anims * sizeof(bs_Anim));
    }

    for(int i = 0; i < data->animations_count; i++)
	bs_loadAnim(data, i, old_anim_count, model);
}

void bs_loadTexturePath(int idx, bs_Model *model, cgltf_texture *c_texture, const char *texture_path) {
    // TODO: Check if already ending with .png (also .jpg support)
    char *path = malloc(strlen(texture_path) + strlen(c_texture->image->name) + sizeof(".png"));
    sprintf(path, "%s/%s.png", texture_path, c_texture->image->name);

    model->texture_names[idx] = path;
}

void bs_loadSkin(cgltf_skin *c_skin, bs_Skin *skin) {
    skin->joints = malloc(c_skin->joints_count * sizeof(bs_Joint));
    skin->joint_count = c_skin->joints_count;

    for(int i = 0; i < skin->joint_count; i++) {
	cgltf_node *c_joint = c_skin->joints[i];
	bs_Joint *joint = skin->joints + i;

	// Set the local matrix
	bs_mat4 local = GLM_MAT4_IDENTITY_INIT;
	glm_translate(local, c_joint->translation);
	glm_quat_rotate(local, c_joint->rotation, local);
	glm_scale(local, c_joint->scale);
	glm_mat4_inv(local, joint->local_inv);

	// Set the inverse bind matrix and regular bind matrix
	cgltf_accessor_read_float(c_skin->inverse_bind_matrices, i, (float*)joint->bind_matrix_inv, 16);
	glm_mat4_inv(joint->bind_matrix_inv, joint->bind_matrix);

	memcpy(skin->joints[i].mat, GLM_MAT4_IDENTITY, sizeof(bs_mat4));

	c_joint->id = i;
    }

    for(int i = 0; i < skin->joint_count; i++) {
	int parent_id = c_skin->joints[i]->parent->id;

	// If parent id is the armature
	if(parent_id == -1) {
	    skin->joints[i].parent = &identity_joint;
	    continue;
	}

	skin->joints[i].parent = skin->joints + parent_id;
    }
}

void bs_cgltfError(int err) {
    switch(err) {
	case cgltf_result_success: return;
	case cgltf_result_unknown_format: printf("GLTF ERROR: \"UNKNOWN FORMAT\"\n"); break;
	case cgltf_result_invalid_json: printf("GLTF ERROR: \"INVALID JSON\"\n"); break;
	case cgltf_result_invalid_gltf: printf("GLTF ERROR: \"INVALID GLTF\"\n"); break;
	case cgltf_result_file_not_found: printf("GLTF ERROR: \"FILE NOT FOUND\"\n"); break;
	default: printf("GLTF ERROR (code %d)\n", err); break;
    }
}

int bs_model(bs_Model *model, const char *model_path, const char *texture_path) {
    cgltf_options options = {0};
    cgltf_data* data = NULL;

    int err;
    err = cgltf_parse_file(&options, model_path, &data);
    if(err != cgltf_result_success) return err;
    err = cgltf_load_buffers(&options, data, model_path);
    if(err != cgltf_result_success) return err;
    
    int path_len = strlen(model_path);
    int mesh_count = data->meshes_count;

    model->texture_names = NULL;
    model->meshes = malloc(mesh_count * sizeof(bs_Mesh));
    model->mesh_count = mesh_count;
    model->prim_count = 0;
    model->anim_count = 0;
    model->anim_offset = 0;
    model->vertex_count = 0;
    model->index_count = 0;
 
    model->name = malloc(path_len + 1);
    strcpy(model->name, model_path);

    model->texture_names = malloc(data->textures_count * sizeof(char *));
    model->texture_count = data->textures_count;
    for(int i = 0; i < data->textures_count; i++)
	bs_loadTexturePath(i, model, data->textures + i, texture_path);

    model->skins = malloc(data->skins_count * sizeof(bs_Skin));
    model->skin_count = data->skins_count;
    for(int i = 0; i < data->skins_count; i++) {
	bs_loadSkin(data->skins + i, model->skins + i);
    }

    for(int i = 0; i < mesh_count; i++) {
	bs_loadMesh(data, model, i);
	model->prim_count += model->meshes[i].prim_count;
    }

    bs_loadAnims(data, model);

    cgltf_free(data);
    return 0;
}

void bs_animate(bs_Anim *anim, int bind_point, int frame) {
    if(anim == NULL) {
	printf("Anim is NULL\n");
	return;
    }

    if(bind_point >= anim->num_mesh_anims) {
	printf("Mesh Anim is NULL\n");
	return;
    }

    bs_MeshAnim *mesh_anim = anim->mesh_anims + bind_point;

    struct {
	int frame;
	int num_frames;
    } buf; 

    frame %= anim->frame_count;
    frame *= anim->joint_count;
    frame += mesh_anim->shader_offset;
    buf.frame = frame;
    buf.num_frames = anim->frame_count;

    bs_selectSSBO(anim_ssbo);
    bs_pushSSBO(&buf, 0, sizeof(buf));
}

void bs_pushAnims() {
    int ssbo_size = anim_offset * sizeof(bs_mat4);
    anim_ssbo = bs_SSBO(NULL, ssbo_size + sizeof(int) * 4, 1);
 
    for(int i = 0; i < anim_count; i++) {
	bs_Anim *anim = anims + i;
	for(int j = 0; j < anim->num_mesh_anims; j++) {
	    bs_MeshAnim *mesh_anim = anim->mesh_anims + j;

	    int size   = mesh_anim->num_joints * anim->frame_count * sizeof(bs_mat4);
	    int offset = (sizeof(int) * 4) + mesh_anim->shader_offset * sizeof(bs_mat4);
	    
	    bs_pushSSBO(mesh_anim->joints, offset, size);
	    free(mesh_anim->joints);
	}

	//free(anim->mesh_anims);
	free(anim->matrices);
    }
}

bs_Anim *bs_anims() {
    return anims;
}

bs_Anim *bs_modelAnims(bs_Model *model) {
    if(model->anim_count == 0)
	return NULL;

    return anims + model->anim_offset;
}

bs_Anim *bs_modelAnimFromName(const char *name, bs_Model *model) {
    for(int i = 0; i < model->anim_count; i++) {
	bs_Anim *anim = anims + model->anim_offset + i;
	if(strcmp(name, anim->name) == 0)
	    return anim;
    }

    return NULL;
}

bs_Anim *bs_animFromName(const char *name) {
    for(int i = 0; i < anim_count; i++) {
	bs_Anim *anim = anims + i;
	if(strcmp(name, anim->name) == 0)
	    return anim;
    }

    return NULL;
}

int bs_numAnims() {
    return anim_count;
}

void bs_freeModel(bs_Model *model) {
    for(int m = 0; m < model->mesh_count; m++) {
	bs_Mesh *mesh = model->meshes + m;
	for(int p = 0; p < mesh->prim_count; p++) {
	    bs_Prim *prim = mesh->prims + p;
	    free(prim->indices);
	    free(prim->vertices);
	}

	free(mesh->prims);
    }

    for(int i = 0; i < model->skin_count; i++) {
	bs_Skin *skin = model->skins + i;

	for(int j = 0; j < skin->joint_count; j++)
	    free(skin->joints);
    }

    // TODO: Free texture names?
    free(model->skins);
    free(model->meshes);
}
