#include <stdlib.h>
//#include <libgen.h>
#include <assert.h>

#include <esdm.h>
// #include <esdm-datatypes.h>

#include "config.h"
#include "nc.h"
#include "ncdispatch.h"

#define NOT_IMPLEMENTED assert(0 && "NOT IMPLEMENTED");

#define debug(...) do{printf("called %s: %d ",__func__, __LINE__); printf(__VA_ARGS__); }while(0)

typedef struct{
  nc_type type;
  int ndims;
  int *dimidsp;
  esdm_dataspace_t * space;
  esdm_dataset_t * dset;
  smd_attr_t * att;
} md_entity_var_t;

// typedef struct {
//   smd_attr_t * dims;
//   smd_attr_t * attrs;
//   smd_attr_t * vars;
// } nc_esdm_md_t;

// This structure contains a flat mday for the metadata name
// it is used up to a specific size of the metadata table before
// the hashmap implementation is beneficial

typedef struct esdm_metadata_t {
	char *json;
  int size;
  smd_attr_t * smd;
} esdm_metadata_t;

// This has to be a vector, but I couldn't do it.

typedef struct metadata_t {
  int size;
  char *json;
  smd_attr_t smd[10];
} metadata_t;

// Some attributes provide information about the dataset as a whole and are called
// global attributes. These are identified by the attribute name together with a blank
// variable name (in CDL) or a special null "global variable" ID (in C or Fortran).

// typedef struct{
//   int ncid;
//   int idp;
//   esdm_container * c;
//   metadata_t attr;
//   metadata_t dims;
//   metadata_t vars;
//   nc_esdm_md_t md;
// } nc_esdm_t;

typedef struct nc_esdm_t{
	char *name;
	esdm_container *container;
  esdm_metadata_t *metadata_dims;
  esdm_metadata_t *metadata_vars;
  esdm_metadata_t *metadata_attr;
	esdm_dataspace_t *dataspace;
//	GHashTable *fragments;
	esdm_status status;
} nc_esdm_t;

// It should be esdm_datatset_t, but it's having conflict
// Also, doesnt't know GHashTable, but I won't use it now.

// this is a temporary hack
static nc_esdm_t * last_file_md = NULL;


static esdm_datatype_t type_nc_to_esdm(nc_type type){
  switch(type){
    case(NC_NAT): return SMD_DTYPE_UNKNOWN;
    case(NC_BYTE): return SMD_DTYPE_INT8;
    case(NC_CHAR): return SMD_DTYPE_CHAR;
    case(NC_SHORT): return SMD_DTYPE_INT16;
    case(NC_INT): return SMD_DTYPE_INT32;
    case(NC_FLOAT): return SMD_DTYPE_FLOAT;
    case(NC_DOUBLE): return SMD_DTYPE_DOUBLE;
    case(NC_UBYTE): return SMD_DTYPE_UINT8;
    case(NC_USHORT): return SMD_DTYPE_UINT16;
    case(NC_UINT): return SMD_DTYPE_UINT32;
    case(NC_INT64): return SMD_DTYPE_INT64;
    case(NC_UINT64): return SMD_DTYPE_UINT64;
    case(NC_STRING): return SMD_DTYPE_STRING;
    default:
      printf("ESDM does not support compound datatypes from NetCDF: %d\n", type);
      return NULL;
  }
}

static nc_type type_esdm_to_nc(esdm_datatype_t type){
  switch(type->type){
    case(SMD_TYPE_UNKNOWN): return NC_NAT;
    case(SMD_TYPE_INT8): return NC_BYTE;
    case(SMD_TYPE_CHAR): return NC_CHAR;
    case(SMD_TYPE_INT16): return NC_SHORT;
    case(SMD_TYPE_INT32): return NC_INT;
    case(SMD_TYPE_FLOAT): return NC_FLOAT;
    case(SMD_TYPE_DOUBLE): return NC_DOUBLE;
    case(SMD_TYPE_UINT8): return NC_UBYTE;
    case(SMD_TYPE_UINT16): return NC_USHORT;
    case(SMD_TYPE_UINT32): return NC_UINT;
    case(SMD_TYPE_INT64): return NC_INT64;
    case(SMD_TYPE_UINT64): return NC_UINT64;
    case(SMD_TYPE_STRING): return NC_STRING;
    default:
      printf("Unsupported datatype: %d\n", type->type);
      return 0;
  }
}


int lookup_md(metadata_t * md, char * name, void ** value, int * pos){
  for(int i=0; i < md->size; i++){
    if(strcmp(name, md->smd[i].name) == 0){
      *value = & md->smd[i].value;
      *pos = i;
      return NC_NOERR;
    }
  }
  return NC_EBADID;
}

int insert_md(esdm_metadata_t ** md, const char * name, void * value){
  assert((*md)->size < 10);

  (*md)->smd = malloc(10*sizeof(esdm_metadata_t)); // Needs to be revisited
  (*md)->smd[(*md)->size].name = strdup(name);
  (*md)->smd[(*md)->size].value = value;
  (*md)->size++;
  return NC_NOERR;
}

// int insert_md(metadata_t * md, const char * name, void * value){
//   assert(md->size < 10);
//   md->kv[md->size].name = strdup(name);
//   md->kv[md->size].value = value;
//   md->size++;
//   return NC_NOERR;
// }

int ESDM_create(const char *path, int cmode, size_t initialsz, int basepe, size_t *chunksizehintp, void* parameters, struct NC_Dispatch* table, NC* ncp){
  const char * realpath = path;

  if(strncmp(path, "esdm:", 5) == 0){
    realpath = & path[5];
  }else if(strncmp(path, "esd:", 4) == 0){
    realpath = & path[4];
  }
  //const char * base = basename(realpath);

  debug("%s %d %d %s\n", realpath, ncp->ext_ncid, ncp->int_ncid, ncp->path);

  nc_esdm_t * e = malloc(sizeof(nc_esdm_t));
  memset(e, 0, sizeof(nc_esdm_t));
//  e->ncid = ncp->ext_ncid;

  //  e->metadata->smd = NULL;
  e->metadata_dims = (esdm_metadata_t *)malloc(sizeof(esdm_metadata_t));
  e->metadata_vars = (esdm_metadata_t *)malloc(sizeof(esdm_metadata_t));
  e->metadata_attr = (esdm_metadata_t *)malloc(sizeof(esdm_metadata_t));

  // This is what I wanted...

  // e->metadata->smd = (smd_attr_t *)malloc(3*sizeof(smd_attr_t));
  // e->metadata->smd[0] = smd_attr_new("dims", SMD_DTYPE_EMPTY, NULL, 0);
  // e->metadata->smd[1] = smd_attr_new("vars", SMD_DTYPE_EMPTY, NULL, 0);
  // e->metadata->smd[2] = smd_attr_new("attr", SMD_DTYPE_EMPTY, NULL, 0);

  // This is what I got...

  e->metadata_dims->smd = smd_attr_new("dims", SMD_DTYPE_EMPTY, NULL, 0);
  e->metadata_vars->smd = smd_attr_new("vars", SMD_DTYPE_EMPTY, NULL, 0);
  e->metadata_attr->smd = smd_attr_new("attr", SMD_DTYPE_EMPTY, NULL, 0);

  e->container = esdm_container_create(realpath);
  ncp->dispatchdata = e;

  last_file_md = e;

  return NC_NOERR;
}

int ESDM_open(const char *path, int mode, int basepe, size_t *chunksizehintp, void* parameters, struct NC_Dispatch* table, NC* ncp){
  const char * realpath = path;

  if(strncmp(path, "esdm:", 5) == 0){
    realpath = & path[5];
  }else if(strncmp(path, "esd:", 4) == 0){
    realpath = & path[4];
  }
  //const char * base = basename(realpath);
  debug("%s %d %d %s\n", realpath, ncp->ext_ncid, ncp->int_ncid, ncp->path);

  nc_esdm_t * e = malloc(sizeof(nc_esdm_t));
  memset(e, 0, sizeof(nc_esdm_t));

  //e->c = esdm_container_create(realpath);
  //TODO load metadata and such
  ncp->dispatchdata = last_file_md;

  return NC_NOERR;
}

int ESDM_redef(int ncid){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM__enddef(int ncid, size_t h_minfree, size_t v_align, size_t v_minfree, size_t r_align){
  NC * ncp;
  int ret = NC_NOERR;
  if((ret = NC_check_id(ncid, (NC**)&ncp)) != NC_NOERR) return (ret);
  nc_esdm_t * e = (nc_esdm_t *) ncp->dispatchdata;
  debug("%d\n", ncid);

  esdm_container_commit(e->container);
  return NC_NOERR;
}

int ESDM_sync(int ncid){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_abort(int ncid){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_close(int ncid, void * b){
  NC * ncp;
  int ret = NC_NOERR;
  if((ret = NC_check_id(ncid, (NC**)&ncp)) != NC_NOERR) return (ret);
  nc_esdm_t * e = (nc_esdm_t *) ncp->dispatchdata;
  debug("%d\n", ncid);
  //esdm_container_destroy(e->c); // TODO disable for now, as we want to reread the file
  // TODO
  return NC_NOERR;
}

int ESDM_set_fill(int ncid, int fillmode, int *old_modep){
  debug("%d %d\n", ncid, fillmode);
  *old_modep = NC_NOFILL;
  return NC_NOERR;
}

int ESDM_def_var_fill(int ncid, int varid, int no_fill, const void *fill_value){
  debug("%d %d\n", ncid, no_fill);
  return NC_NOERR;
}


int ESDM_var_par_access(int ncid, int varid, int access){
  debug("%d: var:%d access:%d\n", ncid, varid, access);
  return NC_NOERR;
}


int ESDM_inq_base_pe(int ncid, int *pe){ // for parallel execution
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_set_base_pe(int ncid, int pe){ // for parallel execution
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_inq_format(int ncid, int *formatp){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_inq_format_extended(int ncid, int *formatp, int* modep){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_inq(int ncid, int *ndimsp, int *nvarsp, int *nattsp, int *unlimdimidp){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_inq_type(int ncid, nc_type xtype, char *name, size_t *size){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_def_dim(int ncid, const char *name, size_t len, int *idp){
  NC * ncp;
  int ret = NC_NOERR;
  if((ret = NC_check_id(ncid, (NC**)&ncp)) != NC_NOERR) return (ret);
  nc_esdm_t * e = (nc_esdm_t *) ncp->dispatchdata;
  // *idp = e->dims.size;
  *idp = e->metadata_dims->size;
  debug("%d: %d\n", ncid, *idp);
  ret = insert_md(& e->metadata_dims, name, (size_t*) len);

  smd_attr_t * new = smd_attr_new(name, SMD_DTYPE_UINT64, & len, *idp);
  // ret = smd_attr_link(e->md.dims, new, 0); // Not being used yet
  if (ret == SMD_ATTR_EEXIST){
    return NC_EINVAL;
  }
  return NC_NOERR;
}

int ESDM_inq_dimid(int ncid, const char *name, int *idp){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_inq_dim(int ncid, int dimid, char *name, size_t *lenp){
  NC * ncp;
  int ret = NC_NOERR;
  if((ret = NC_check_id(ncid, (NC**)&ncp)) != NC_NOERR) return (ret);
  nc_esdm_t * e = (nc_esdm_t *) ncp->dispatchdata;
  debug("%d %d %s\n", ncid, dimid, name);

  assert(e->metadata_dims->size > dimid);

  smd_attr_t * smd = & e->metadata_dims->smd[dimid];
  if(name != NULL){
    strcpy(name, smd->name);
  }
  if(lenp != NULL){
    *lenp = (size_t) smd->value;
  }

  return NC_NOERR;
}

int ESDM_inq_unlimdim(int ncid, int *unlimdimidp){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_rename_dim(int ncid, int dimid, const char *name){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_inq_att(int ncid, int varid, const char *name, nc_type *datatypep, size_t *lenp){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_inq_attid(int ncid, int varid, const char *name, int *attnump){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_inq_attname(int ncid, int varid, int attnum, char *name){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_rename_att(int ncid, int varid, const char *name, const char *newname){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_del_att(int ncid, int varid, const char *name){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_get_att(int ncid, int varid, const char* name, void* value, nc_type t){
  esdm_datatype_t etype = type_nc_to_esdm(t);
  if(etype == NULL) {
    return NC_EINVAL;
  }
  int ret;
  NC * ncp;
  if((ret = NC_check_id(ncid, (NC**)& ncp)) != NC_NOERR) return (ret);
  nc_esdm_t * e = (nc_esdm_t *) ncp->dispatchdata;
  debug("%d %d %s\n", ncid, varid, name);

  smd_attr_t * att;
  if(varid == NC_GLOBAL){
    // att = e->md.vars;// Not being used yet
  }else{
    assert(e->metadata_vars->size > varid);
    md_entity_var_t * smd = e->metadata_vars->smd[varid].value;
    if(smd->att == NULL){
      return NC_EINVAL;
    }
    att = smd->att;
  }
  int pos = smd_find_position_by_name(att, name);
  if(pos < 0) return NC_EINVAL;

  smd_attr_t * child = smd_attr_get_child(att, pos);
  if(etype->type != child->type->type) return NC_EINVAL;
  smd_attr_copy_value(child, value);
  return NC_NOERR;
}

int ESDM_put_att(int ncid, int varid, const char *name, nc_type datatype,
	   size_t len, const void *value, nc_type type){
  assert(type == datatype);
  esdm_datatype_t etype = type_nc_to_esdm(datatype);
  if(etype == NULL) {
    return NC_EINVAL;
  }
  int ret;
  NC * ncp;
  if((ret = NC_check_id(ncid, (NC**)& ncp)) != NC_NOERR) return (ret);
  nc_esdm_t * e = (nc_esdm_t *) ncp->dispatchdata;
  debug("%d %d %s\n", ncid, varid, name);

  smd_attr_t * att;
  if(varid == NC_GLOBAL){
    // att = e->md.vars; // Not being used yet
  }else{
    assert(e->metadata_vars->size > varid);
    md_entity_var_t * smd = e->metadata_vars->smd[varid].value;
    if(smd->att == NULL){
      smd->att = smd_attr_new("attr", SMD_DTYPE_EMPTY, NULL, 0);
    }
    att = smd->att;
  }
  assert(len == 1);
  smd_attr_t * new;
  if(datatype == NC_STRING){
    new = smd_attr_new(name, etype, *(void**) value, 0);
  }else{
    new = smd_attr_new(name, etype, value, 0);
  }
  smd_attr_link(att, new, 1);

  return NC_NOERR;
}

int ESDM_def_var(int ncid, const char *name, nc_type xtype,
            int ndims, const int *dimidsp, int *varidp){
  NC * ncp;
  int ret = NC_NOERR;
  if((ret = NC_check_id(ncid, (NC**)&ncp)) != NC_NOERR) return (ret);
  nc_esdm_t * e = (nc_esdm_t *) ncp->dispatchdata;

  *varidp = e->metadata_vars->size;
  debug("%d: varid: %d\n", ncid, *varidp);

  smd_attr_t * new = smd_attr_new(name, SMD_DTYPE_EMPTY, NULL, *varidp);
  // ret = smd_attr_link(e->md.vars, new, 0); // Not being used yet
  if (ret == SMD_ATTR_EEXIST){
    return NC_EINVAL;
  }

  md_entity_var_t * evar = malloc(sizeof(md_entity_var_t));
  memset(evar, 0, sizeof(md_entity_var_t));

  evar->type = xtype;
  evar->ndims = ndims;
  evar->dimidsp = malloc(sizeof(int) * ndims);

  int64_t bounds[ndims];
  for(int i=0; i < ndims; i++){
    int dimid = dimidsp[i];
    assert(e->metadata_dims->size > dimid);
    smd_attr_t * md = & e->metadata_dims->smd[dimid];
    size_t val = (size_t) md->value;
    evar->dimidsp[i] = dimid;
    printf("%d %s %zd\n", dimidsp[i], md->name, val);
    bounds[i] = val;
  }

  esdm_datatype_t typ = type_nc_to_esdm(xtype);
  if(typ == SMD_DTYPE_UNKNOWN){
    return NC_EBADTYPE;
  }
  esdm_dataspace_t * dataspace = esdm_dataspace_create(ndims, bounds, typ);
  esdm_dataset_t * dataset = esdm_dataset_create(e->container, name, dataspace, NULL);
  if(dataset == NULL){
    return NC_EBADID;
  }
  evar->dset = dataset;
  evar->space = dataspace;
  insert_md(& e->metadata_vars, name, evar);

  return NC_NOERR;
}

int ESDM_inq_varid(int ncid, const char *name, int *varidp){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_rename_var(int ncid, int varid, const char *name){
  debug("%d\n", ncid);
  return NC_NOERR;
}

int ESDM_get_vars(int ncid, int varid, const size_t *startp, const size_t *countp,
             const ptrdiff_t *stridep, const void *data, nc_type mem_nc_type){
//  debug("%d\n", ncid);

  NC * ncp;
  int ret = NC_NOERR;
  if((ret = NC_check_id(ncid, (NC**)&ncp)) != NC_NOERR) return (ret);
  nc_esdm_t * e = (nc_esdm_t *) ncp->dispatchdata;
  assert(e->metadata_vars->size > varid);
  md_entity_var_t * smd = e->metadata_vars->smd[varid].value;
  debug("%d type: %d buff: %p %p %p %p\n", ncid, mem_nc_type, data, startp, countp, stridep);
  if(mem_nc_type != smd->type){
    return NC_EBADTYPE;
  }
  // check the dimensions we actually want to write
  int access_all = 1;
  esdm_dataspace_t * space = smd->space;
  for(int i=0; i < smd->ndims; i++){
    printf(" - %zu %zu\n", startp[i], countp[i]);
    if(startp[i] != 0 || countp[i] != space->size[i]){
      access_all = 0;
      break;
    }
  }
  if(access_all){
    ret = esdm_read(smd->dset, data, space);
    if(ret != ESDM_SUCCESS){
      return NC_EINVAL;
    }
  }else{
    int64_t size[smd->ndims];
    int64_t offset[smd->ndims];
    for(int i=0; i < smd->ndims; i++){
      size[i] = countp[i];
      offset[i] = startp[i];
    }
    esdm_dataspace_t * subspace = esdm_dataspace_subspace(space, smd->ndims, size, offset);
    ret = esdm_read(smd->dset, data, subspace);
    if(ret != ESDM_SUCCESS){
      esdm_dataspace_destroy(subspace);
      return NC_EINVAL;
    }
    esdm_dataspace_destroy(subspace);
  }

  return NC_NOERR;
}

int ESDM_get_vara(int ncid, int varid, const size_t *startp,
             const size_t *countp, void *ip, int memtype)
{
  debug("%d\n", ncid);
  return ESDM_get_vars(ncid, varid, startp, countp, NULL, ip, memtype);
}

int ESDM_put_vars(int ncid, int varid, const size_t *startp, const size_t *countp,
             const ptrdiff_t *stridep, const void *data, nc_type mem_nc_type){
  NC * ncp;
  int ret = NC_NOERR;
  if((ret = NC_check_id(ncid, (NC**)&ncp)) != NC_NOERR) return (ret);
  nc_esdm_t * e = (nc_esdm_t *) ncp->dispatchdata;
  assert(e->metadata_vars->size > varid);
  md_entity_var_t * smd = e->metadata_vars->smd[varid].value;
  debug("%d type: %d buff: %p %p %p %p\n", ncid, mem_nc_type, data, startp, countp, stridep);
  if(mem_nc_type != smd->type){
    return NC_EBADTYPE;
  }
  // check the dimensions we actually want to write
  int access_all = 1;
  esdm_dataspace_t * space = smd->space;
  for(int i=0; i < smd->ndims; i++){
    printf(" - %zu %zu\n", startp[i], countp[i]);
    if(startp[i] != 0 || countp[i] != space->size[i]){
      access_all = 0;
      break;
    }
  }
  if(access_all){
    ret = esdm_write(smd->dset, data, space);
    if(ret != ESDM_SUCCESS){
      return NC_EINVAL;
    }
  }else{
    int64_t size[smd->ndims];
    int64_t offset[smd->ndims];
    for(int i=0; i < smd->ndims; i++){
      size[i] = countp[i];
      offset[i] = startp[i];
    }
    esdm_dataspace_t * subspace = esdm_dataspace_subspace(space, smd->ndims, size, offset);
    ret = esdm_write(smd->dset, data, subspace);
    if(ret != ESDM_SUCCESS){
      esdm_dataspace_destroy(subspace);
      return NC_EINVAL;
    }
    esdm_dataspace_destroy(subspace);
  }

  return NC_NOERR;
}

int ESDM_put_vara(int ncid, int varid, const size_t *startp,
             const size_t *countp, const void *op, int memtype)
{
  debug("%d\n", ncid);
  return ESDM_put_vars(ncid, varid, startp, countp, NULL, op, memtype);
}

int ESDM_inq_var_all(int ncid, int varid, char *name, nc_type *xtypep, int *ndimsp, int *dimidsp, int *nattsp, int *shufflep, int *deflatep, int *deflate_levelp, int *fletcher32p, int *contiguousp, size_t *chunksizesp, int *no_fill, void *fill_valuep, int *endiannessp, unsigned int* idp, size_t* nparamsp, unsigned int* params ){
  NC * ncp;
  int ret = NC_NOERR;
  if((ret = NC_check_id(ncid, (NC**)&ncp)) != NC_NOERR) return (ret);
  nc_esdm_t * e = (nc_esdm_t *) ncp->dispatchdata;
  debug("%d %d\n", ncid, varid);

  smd_attr_t * smd = & e->metadata_vars->smd[varid];
  assert(smd != NULL);
  md_entity_var_t * evar = (md_entity_var_t *) smd->value;
  assert(evar != NULL);

  if(name != NULL){
    strcpy(name, smd->name);
  }
  if(xtypep){
    *xtypep = evar->type;
  }
  if(ndimsp){
    *ndimsp = evar->ndims;
  }
  if(dimidsp){
    for(int i=0; i < evar->ndims; i++){
      dimidsp[i] = evar->dimidsp[i];
    }
  }
  // esdm_dataset_t * dataset
  return NC_NOERR;
}


static NC_Dispatch esdm_dispatcher = {
  NC_FORMATX_ESDM,

  ESDM_create,
  ESDM_open,

  ESDM_redef,
  ESDM__enddef,
  ESDM_sync,
  ESDM_abort,
  ESDM_close,
  ESDM_set_fill,
  NC_NOTNC3_inq_base_pe,
  NC_NOTNC3_set_base_pe,
  ESDM_inq_format,
  ESDM_inq_format_extended,

  ESDM_inq,
  ESDM_inq_type,

  ESDM_def_dim,
  ESDM_inq_dimid,
  ESDM_inq_dim,
  ESDM_inq_unlimdim,
  ESDM_rename_dim,

  ESDM_inq_att,
  ESDM_inq_attid,
  ESDM_inq_attname,
  ESDM_rename_att,
  ESDM_del_att,
  ESDM_get_att,
  ESDM_put_att,

  ESDM_def_var,
  ESDM_inq_varid,
  ESDM_rename_var,
  ESDM_get_vara,
  ESDM_put_vara,
  ESDM_get_vars,
  ESDM_put_vars,
  NCDEFAULT_get_varm,
  NCDEFAULT_put_varm,

  ESDM_inq_var_all,
  ESDM_var_par_access,
  ESDM_def_var_fill
};

NC_Dispatch* esdm_dispatch_table = NULL;

int NC_ESDM_initialize(void) {
   int ret = NC_NOERR;

   esdm_init();
   esdm_dispatch_table = &esdm_dispatcher;
   return ret;
}

int NC_ESDM_finalize(void){
  esdm_finalize();
  return NC_NOERR;
}
