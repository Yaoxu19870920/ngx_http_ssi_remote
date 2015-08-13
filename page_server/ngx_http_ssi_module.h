#ifndef _NGX_HTTP_SSI_FILTER_H_INCLUDED_
#define _NGX_HTTP_SSI_FILTER_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#define NGX_HTTP_SSI_MAX_PARAMS       16

#define NGX_HTTP_SSI_COMMAND_LEN      32
#define NGX_HTTP_SSI_PARAM_LEN        32
#define NGX_HTTP_SSI_PARAMS_N         4

#define NGX_HTTP_SSI_COND_IF          1
#define NGX_HTTP_SSI_COND_ELSE        2

#define NGX_HTTP_SSI_NO_ENCODING      0
#define NGX_HTTP_SSI_URL_ENCODING     1
#define NGX_HTTP_SSI_ENTITY_ENCODING  2

typedef struct {
    ngx_hash_t                hash;
    ngx_hash_keys_arrays_t    commands;
} ngx_http_ssi_main_conf_t;

typedef struct {
    ngx_str_t   	key;
    ngx_chain_t 	**value;
	int		count;
} ngx_ssi_content_t;

typedef struct {
    ngx_http_request_t    *key;
    ngx_chain_t   		 **value;
} ngx_request_chain_t;

typedef struct {
    ngx_buf_t                *buf;

    u_char                   *pos;
    u_char                   *copy_start;
    u_char                   *copy_end;

    ngx_uint_t                key;
    ngx_str_t                 command;
    ngx_array_t               params;
    ngx_table_elt_t          *param;
    ngx_table_elt_t           params_array[NGX_HTTP_SSI_PARAMS_N];

    ngx_chain_t              *in;
    ngx_chain_t              *out;
    ngx_chain_t             **last_out;
    ngx_chain_t              *busy;
    ngx_chain_t              *free;

    ngx_uint_t                state;
    ngx_uint_t                saved_state;
    size_t                    saved;
    size_t                    looked;

    size_t                    value_len;

    ngx_list_t               *variables;
    ngx_array_t              *blocks;

#if (NGX_PCRE)
    ngx_uint_t                ncaptures;
    int                      *captures;
    u_char                   *captures_data;
#endif

    unsigned                  conditional:2;
    unsigned                  encoding:2;
    unsigned                  block:1;
    unsigned                  output:1;
    unsigned                  output_chosen:1;
	//正在被等待的子请求
    ngx_http_request_t       *wait;
    void                     *value_buf;
    ngx_str_t                 timefmt;
    ngx_str_t                 errmsg;

	//key是uri，value是子请求的args
	ngx_keyval_t			**ssi_map;
	//key是子请求，value是子请求的子请求对应的碎片
	ngx_array_t			    *ssi_content_map;
	//key是uri，value是子请求对应的若干个碎片，在子请求生成时候，相等于ssi_content_mao中的value
	ngx_ssi_content_t		**ssi_content_map_temp;
	//所有的碎片服务总数
	unsigned				  ssi_server_sum:8;
	//当前子请求的个数
	unsigned				  ssi_sub_count:8;
	//除了ssi_router_default之外的
	unsigned				  ssi_server_nodefault_sum:8;
	//碎片以及碎片间的content组成的chain
	ngx_chain_t				 *ssi_content;
	//发现了include
	unsigned				  ssi_found:1;
} ngx_http_ssi_ctx_t;


typedef ngx_int_t (*ngx_http_ssi_command_pt) (ngx_http_request_t *r,
    ngx_http_ssi_ctx_t *ctx, ngx_str_t **);


typedef struct {
    ngx_str_t                 name;
    ngx_uint_t                index;

    unsigned                  mandatory:1;
    unsigned                  multiple:1;
} ngx_http_ssi_param_t;


typedef struct {
    ngx_str_t                 name;
    ngx_http_ssi_command_pt   handler;
    ngx_http_ssi_param_t     *params;

    unsigned                  conditional:2;
    unsigned                  block:1;
    unsigned                  flush:1;
} ngx_http_ssi_command_t;


extern ngx_module_t  ngx_http_ssi_filter_module;


#endif /* _NGX_HTTP_SSI_FILTER_H_INCLUDED_ */
