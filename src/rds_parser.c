
/*
 * Copyright (C) agentzh
 */

#ifndef DDEBUG
#define DDEBUG 1
#endif
#include "ddebug.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "rds_parser.h"


static int rds_parse(lua_State *L);
static int rds_parse_header(lua_State *L, rds_buf_t *b, rds_header_t *header);
static int rds_parse_col(lua_State *L, rds_buf_t *b, rds_column_t *col);
static void free_rds_cols(rds_column_t *cols, size_t ncols);


static char *rds_null = "null";


static const struct luaL_Reg rds_parser[] = {
    {"parse", rds_parse},
    {NULL, NULL}
};


int
luaopen_rds_parser(lua_State *L)
{
    luaL_register(L, "rds.parser", rds_parser);

    lua_pushlightuserdata(L, rds_null);
    lua_setfield(L, -2, "null");

    return 1;
}


static int
rds_parse(lua_State *L)
{
    rds_buf_t           b;
    size_t              len;
    int                 rc;
    rds_header_t        h;
    rds_column_t       *cols;
    int                 i;

    if (lua_gettop(L) != 1) {
        lua_pushnil(L);
        lua_pushfstring(L, "expecting 1 argument but got %d", lua_gettop(L));
        return 2;
    }

    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushfstring(L, "expecting string argument but got type %s",
                luaL_typename(L, 1));
        return 2;
    }

    b.start = (u_char *) lua_tolstring(L, 1, &len);
    b.end = b.start + len;

    b.pos = b.start;
    b.last = b.end;

    rc = rds_parse_header(L, &b, &h);
    if (rc != 0) {
        return rc;
    }

    cols = malloc(h.col_count * sizeof(rds_column_t));
    if (cols == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "out of memory");
        return 2;
    }

    for (i = 0; i < h.col_count; i++) {
        rc = rds_parse_col(L, &b, &cols[i]);
        if (rc != 0) {
            free_rds_cols(cols, i);
            return rc;
        }
    }

    lua_createtable(L, 0 /* narr */, 4 /* nrec */);

    lua_pushinteger(L, h.std_errcode);
    lua_setfield(L, -2, "errcode");

    if (h.errstr.len > 0) {
        lua_pushlstring(L, (char *) h.errstr.data, h.errstr.len);
        lua_setfield(L, -2, "errstr");
    }

    if (h.insert_id) {
        lua_pushinteger(L, h.insert_id);
        lua_setfield(L, -2, "insert_id");
    }

    if (h.affected_rows) {
        lua_pushinteger(L, h.affected_rows);
        lua_setfield(L, -2, "affected_rows");
    }

    return 1;
}


static int
rds_parse_header(lua_State *L, rds_buf_t *b, rds_header_t *header)
{
    ssize_t          rest;

    rest = sizeof(uint8_t)      /* endian type */
         + sizeof(uint32_t)     /* format version */
         + sizeof(uint8_t)      /* result type */

         + sizeof(uint16_t)     /* standard error code */
         + sizeof(uint16_t)     /* driver-specific error code */

         + sizeof(uint16_t)     /* driver-specific errstr len */
         + 0                    /* driver-specific errstr data */
         + sizeof(uint64_t)     /* affected rows */
         + sizeof(uint64_t)     /* insert id */
         + sizeof(uint16_t)     /* column count */
         ;

    if (b->last - b->pos < rest) {
        lua_pushnil(L);
        lua_pushliteral(L, "header part is incomplete");
        return 2;
    }

    /* TODO check endian type */

    b->pos += sizeof(uint8_t);

    /* check RDS format version number */

    if ( *(uint32_t *) b->pos != (uint32_t) resty_dbd_stream_version) {
        lua_pushnil(L);
        lua_pushfstring(L, "found RDS format version %d, "
                "but we can only handle version %d",
                (int) *(uint32_t *) b->pos, resty_dbd_stream_version);
        return 2;
    }

    dd("RDS format version: %d", (int) *(uint32_t *) b->pos);

    b->pos += sizeof(uint32_t);

    /* check RDS result type */

    if (*b->pos != 0) {
        lua_pushnil(L);
        lua_pushliteral(L, "rds: RDS result type must be 0 for now");
        return 2;
    }

    b->pos++;

    /* save the standard error code */

    header->std_errcode = *(uint16_t *) b->pos;

    b->pos += sizeof(uint16_t);

    /* save the driver-specific error code */

    header->drv_errcode = *(uint16_t *) b->pos;

    b->pos += sizeof(uint16_t);

    /* save the error string length */

    header->errstr.len = *(uint16_t *) b->pos;

    b->pos += sizeof(uint16_t);

    dd("errstr len: %d", (int) header->errstr.len);

    /* check the rest data's size */

    rest = header->errstr.len
         + sizeof(uint64_t)     /* affected rows */
         + sizeof(uint64_t)     /* insert id */
         + sizeof(uint16_t)     /* column count */
         ;

    if (b->last - b->pos < rest) {
        lua_pushnil(L);
        lua_pushliteral(L, "header part is incomplete");
        return 2;
    }

    /* save the error string data */

    header->errstr.data = b->pos;

    b->pos += header->errstr.len;

    /* save affected rows */

    header->affected_rows = *(uint64_t *) b->pos;

    b->pos += sizeof(uint64_t);

    /* save insert id */

    header->insert_id = *(uint64_t *)b->pos;

    b->pos += sizeof(uint64_t);

    /* save column count */

    header->col_count = *(uint16_t *) b->pos;

    b->pos += sizeof(uint16_t);

    dd("saved column count: %d", (int) header->col_count);

    return 0;
}


static int
rds_parse_col(lua_State *L, rds_buf_t *b, rds_column_t *col)
{
    ssize_t         rest;

    rest = sizeof(uint16_t)         /* std col type */
         + sizeof(uint16_t)         /* driver col type */
         + sizeof(uint16_t)         /* col name str len */
         ;

    if (b->last - b->pos < rest) {
        lua_pushnil(L);
        lua_pushliteral(L, "column spec is incomplete");
        return 2;
    }

    /* save standard column type */
    col->std_type = *(uint16_t *) b->pos;
    b->pos += sizeof(uint16_t);

    /* save driver-specific column type */
    col->drv_type = *(uint16_t *) b->pos;
    b->pos += sizeof(uint16_t);

    /* read column name string length */

    col->name.len = *(uint16_t *) b->pos;
    b->pos += sizeof(uint16_t);

    if (col->name.len == 0) {
        lua_pushnil(L);
        lua_pushliteral(L, "column name empty");
        return 2;
    }

    rest = col->name.len;

    if (b->last - b->pos < rest) {
        lua_pushnil(L);
        lua_pushliteral(L, "column name string is incomplete");
        return 2;
    }

    /* save the column name string data */

    col->name.data = malloc(col->name.len);
    if (col->name.data == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "out of memory");
        return 2;
    }

    memcpy(col->name.data, b->pos, col->name.len);
    b->pos += col->name.len;

    dd("saved column name \"%.*s\" (len %d, offset %d)",
            (int) col->name.len, col->name.data,
            (int) col->name.len, (int) (b->pos - b->start));

    return 0;
}


static void
free_rds_cols(rds_column_t *cols, size_t ncols)
{
    int         i;

    for(i = 0; i < ncols; i++) {
        free(cols[i].name.data);
    }

    free(cols);
}

