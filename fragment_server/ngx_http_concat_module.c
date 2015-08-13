#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
    ngx_flag_t   enable;
    ngx_uint_t   max_files;
    ngx_flag_t   unique;
    ngx_str_t    delimiter;
    ngx_flag_t   ignore_file_error;

	ngx_flag_t   with_file_size;

    ngx_hash_t   types;
    ngx_array_t *types_keys;
} ngx_http_concat_loc_conf_t;


static ngx_int_t ngx_http_concat_add_path(ngx_http_request_t *r,
    ngx_array_t *uris, size_t max, ngx_str_t *path, u_char *p, u_char *v);
static ngx_int_t ngx_http_concat_init(ngx_conf_t *cf);
static void *ngx_http_concat_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_concat_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);


// add by liuyan
typedef struct {
	ngx_file_t	*file;
	ngx_str_t	 path;
	ngx_int_t    id;
} ngx_http_file_wrapper_t;

typedef struct {
	ngx_int_t	 subreqs;
	ngx_str_t   *root;
	ngx_chain_t *out;	
	ngx_int_t    length;
	ngx_array_t *subids;
	time_t		 last_modified;
} ngx_http_concat_ctx_t;

typedef struct {
	ngx_int_t    subid;	
	ngx_uint_t   done;	
} ngx_subid_node_t;

typedef enum {
	MIDDLE_FILE = 0x0,
	FIRST_FILE = 0x1,
	LAST_FILE = 0x2
} ngx_file_index_t;

static ngx_str_t  ngx_http_concat_default_types[] = {
    ngx_string("application/x-javascript"),
    ngx_string("text/css"),
    ngx_null_string
};

static ngx_int_t ngx_http_get_file_subrequest(ngx_http_request_t *r, 
											  ngx_str_t *fullname, ngx_int_t id);
static ngx_int_t subrequest_post_handler(ngx_http_request_t *r, void *data, ngx_int_t rc);

static ngx_int_t ngx_build_buffer(ngx_http_request_t *r, ngx_str_t *filename,
								  ngx_int_t openf, ngx_open_file_info_t *of, ngx_file_index_t fid,
				 				  ngx_chain_t *out);

static void ngx_http_post_handler(ngx_http_request_t *r);


static ngx_command_t  ngx_http_concat_commands[] = {

    { ngx_string("concat"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_concat_loc_conf_t, enable),
      NULL },

    { ngx_string("concat_max_files"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_concat_loc_conf_t, max_files),
      NULL },

    { ngx_string("concat_unique"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_concat_loc_conf_t, unique),
      NULL },

    { ngx_string("concat_types"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_types_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_concat_loc_conf_t, types_keys),
      &ngx_http_concat_default_types[0] },

    { ngx_string("concat_delimiter"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_concat_loc_conf_t, delimiter),
      NULL },

    { ngx_string("concat_ignore_file_error"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_concat_loc_conf_t, ignore_file_error),
      NULL },

	{ ngx_string("concat_with_file_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_concat_loc_conf_t, with_file_size),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_concat_module_ctx = {
    NULL,                                /* preconfiguration */
    ngx_http_concat_init,                /* postconfiguration */

    NULL,                                /* create main configuration */
    NULL,                                /* init main configuration */

    NULL,                                /* create server configuration */
    NULL,                                /* merge server configuration */

    ngx_http_concat_create_loc_conf,     /* create location configuration */
    ngx_http_concat_merge_loc_conf       /* merge location configuration */
};


ngx_module_t  ngx_http_concat_module = {
    NGX_MODULE_V1,
    &ngx_http_concat_module_ctx,         /* module context */
    ngx_http_concat_commands,            /* module directives */
    NGX_HTTP_MODULE,                     /* module type */
    NULL,                                /* init master */
    NULL,                                /* init module */
    NULL,                                /* init process */
    NULL,                                /* init thread */
    NULL,                                /* exit thread */
    NULL,                                /* exit process */
    NULL,                                /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_concat_handler(ngx_http_request_t *r)
{
    size_t                      root, last_len;
    u_char                     *p, *v, *e, *last, *last_type;
    ngx_int_t                   openf, rc;
    ngx_str_t                  *uri, *filename, path;
    ngx_uint_t                  i, j, level, oknum, errnum;
	ngx_file_index_t			fid;
    ngx_flag_t                  timestamp;
    ngx_array_t                 uris;
    ngx_open_file_info_t        of;
    ngx_http_core_loc_conf_t   *ccf;
    ngx_http_concat_loc_conf_t *clcf;
	ngx_http_concat_ctx_t      *ctx;
    if (r->uri.data[r->uri.len - 1] != '/') {
        return NGX_DECLINED;
    }

	// 浠呮敮鎸丟ET銆丠EAD璇锋眰
    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_DECLINED;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_concat_module);

    if (!clcf->enable) {
        return NGX_DECLINED;
    }

    if (r->args.len < 2 || r->args.data[0] != '?') {
        return NGX_DECLINED;
    }

    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

	ctx = ngx_http_get_module_ctx(r, ngx_http_concat_module);
	if(ctx == NULL) {
		ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_concat_ctx_t));
		if(ctx == NULL) {
			return NGX_HTTP_INTERNAL_SERVER_ERROR;
		}
		ngx_http_set_ctx(r, ctx, ngx_http_concat_module);
	}

	ctx->length = 0;//
	ctx->root = ngx_pcalloc(r->pool, sizeof(ngx_str_t));//
	ctx->out = ngx_pcalloc(r->pool, sizeof(ngx_chain_t));//
	ctx->subids = ngx_array_create(r->pool, 8, sizeof(ngx_subid_node_t));//
	if(ctx->root == NULL || ctx->out == NULL || ctx->subids == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    last = ngx_http_map_uri_to_path(r, &path, &root, 0);
    if (last == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    path.len = last - path.data;

	ctx->root->data = ngx_pcalloc(r->pool, path.len*sizeof(u_char));//
	ctx->root->len = path.len;//
	ngx_memcpy(ctx->root->data, path.data, path.len);//

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http concat root: \"%V\"", &path);

#if (NGX_SUPPRESS_WARN)
    ngx_memzero(&uris, sizeof(ngx_array_t));
#endif

    if (ngx_array_init(&uris, r->pool, 8, sizeof(ngx_str_t)) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

	// 鍙傛暟瑙ｆ瀽鍒皍ris涓紝鍙傛暟浠�','鍒嗗壊
    e = r->args.data + r->args.len;
    for (p = r->args.data + 1, v = p, timestamp = 0; p != e; p++) {

        if (*p == ',') {	// 姝ｅ父鏂囦欢鍚�
            if (p == v || timestamp == 1) {
                v = p + 1;
                timestamp = 0;
                continue;
            }

            rc = ngx_http_concat_add_path(r, &uris, clcf->max_files, &path,
                                          p, v);
            if (rc != NGX_OK) {
                return rc;
            }

            v = p + 1;

        } else if (*p == '?') {	
            if (timestamp == 1) {
                v = p;
                continue;
            }

            rc = ngx_http_concat_add_path(r, &uris, clcf->max_files, &path,
                                          p, v);
            if (rc != NGX_OK) {
                return rc;
            }

            v = p;
            timestamp = 1;
        }
    }

	// 鏈€鍚庝竴涓枃浠�
    if (p - v > 0 && timestamp == 0) {
        rc = ngx_http_concat_add_path(r, &uris, clcf->max_files, &path, p, v);
        if (rc != NGX_OK) {
            return rc;
        }
    }	

    ccf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    ctx->last_modified = 0;
    last_len = 0;
    last_type = NULL;
    uri = uris.elts;
	oknum = 0;
	errnum = 0;

    for (i = 0; i < uris.nelts; i++) {
        filename = uri + i;

        for (j = filename->len - 1; j > 1; j--) {
            if (filename->data[j] == '.' && filename->data[j - 1] != '/') {

                r->exten.len = filename->len - j - 1;
                r->exten.data = &filename->data[j + 1];
                break;

            } else if (filename->data[j] == '/') {
                break;
            }
        }

        r->headers_out.content_type.len = 0;
        if (ngx_http_set_content_type(r) != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        r->headers_out.content_type_lowcase = NULL;
        if (ngx_http_test_content_type(r, &clcf->types) == NULL) {
			ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
						  "invalid content type");
            return NGX_HTTP_BAD_REQUEST;
        }

        if (clcf->unique) { /* test if all the content types are the same */
            if ((i > 0)
                && (last_len != r->headers_out.content_type_len
                    || (last_type != NULL
                        && r->headers_out.content_type_lowcase != NULL
                        && ngx_memcmp(last_type,
                                      r->headers_out.content_type_lowcase,
                                      last_len) != 0)))
            {
				ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
							  "only unique content type is allowed");
                return NGX_HTTP_BAD_REQUEST;
            }

            last_len = r->headers_out.content_type_len;
            last_type = r->headers_out.content_type_lowcase;
        }

        ngx_memzero(&of, sizeof(ngx_open_file_info_t));

        of.read_ahead = ccf->read_ahead;
        of.directio = ccf->directio;
        of.valid = ccf->open_file_cache_valid;
        of.min_uses = ccf->open_file_cache_min_uses;
        of.errors = ccf->open_file_cache_errors;
        of.events = ccf->open_file_cache_events;

		openf = ngx_open_cached_file(ccf->open_file_cache, filename, &of, r->pool);
        if (openf != NGX_OK) {

			errnum++;

            switch (of.err) {

            case 0:
                return NGX_HTTP_INTERNAL_SERVER_ERROR;

            case NGX_ENOENT:		// no such file or dir

                level = NGX_LOG_ERR;

				// subrequest
				rc = ngx_http_get_file_subrequest(r, filename, i);
				if(rc != NGX_OK) {
					ngx_log_error(level, r->connection->log, ngx_errno,
								  "\"%V\" subrequest failed", filename);
				} else {
					ctx->subreqs++;//
					ngx_subid_node_t *node = ngx_array_push(ctx->subids);
					node->subid = i;
					node->done = 0;
				}

                rc = NGX_HTTP_NOT_FOUND;
                break;

            case NGX_EACCES:		// permission denied
			case NGX_EPERM:			// operation not permitted
            case NGX_ENOTDIR:		// not a dir
            case NGX_ENAMETOOLONG:	// filename too long

                level = NGX_LOG_ERR;
                rc = NGX_HTTP_FORBIDDEN;
                break;

            default:

                level = NGX_LOG_CRIT;
                rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
                break;
            }

			// 鎵撲笅鏃ュ織
			ngx_log_error(level, r->connection->log, ngx_errno,
						  "%s \"%V\" failed [%d]", of.failed, filename, rc);

            if (!clcf->with_file_size
				&& clcf->ignore_file_error
                && (rc == NGX_HTTP_NOT_FOUND || rc == NGX_HTTP_FORBIDDEN))
            {
                continue;
            }
			
        } else {

			oknum++;
			if (of.mtime > ctx->last_modified) {
				ctx->last_modified = of.mtime;
			}
		}

		fid = MIDDLE_FILE;
		if(i == 0) {
			fid |= FIRST_FILE;
		}
		if(i == uris.nelts - 1) {
			fid |= LAST_FILE;
		} 
		// 鍒涘缓buffer
		rc = ngx_build_buffer(r, filename, openf, &of, fid, ctx->out);
		if(rc != NGX_OK) {
			return rc;
		}
		
    } // end of for loop

	ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
				  "\t1111111111\tSubreqs[%d]\t"
				  "@@@@@ 璇锋眰鏂囦欢鏁癧%d], 鎴愬姛[%d], 澶辫触[%d] 鎬婚暱搴%d] @@@@@", 
				  ctx->subreqs, uris.nelts, oknum, errnum, ctx->length);

	if(ctx->subreqs > 0) {
		return NGX_DONE;
	}

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = ctx->length;
    r->headers_out.last_modified_time = ctx->last_modified;

    if (ctx->out->buf == NULL) {
        r->header_only = 1;
    }

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_output_filter(r, ctx->out);
}

static ngx_int_t
ngx_build_buffer(ngx_http_request_t *r, ngx_str_t *filename, 
				 ngx_int_t openf, ngx_open_file_info_t *of, ngx_file_index_t fid,
				 ngx_chain_t *out)
{
	ngx_http_concat_ctx_t	   *ctx;
    ngx_http_concat_loc_conf_t *clcf;
	ngx_str_t				   *root;
    u_char                     *c, *buffer;
	unsigned int                j, buflen, namelen, size;
	ngx_buf_t				   *b;
    ngx_chain_t                *cl, *ch;

	ctx = ngx_http_get_module_ctx(r, ngx_http_concat_module);
	root = ctx->root;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_concat_module);
	if(clcf->with_file_size) {
		namelen = filename->len - root->len;	

		// ------------------------------ for debug
		//char str[512] = {0};
		//sprintf(str, "%u", namelen);
		//write(STDOUT_FILENO, str, strlen(str));
		//write(STDOUT_FILENO, "\t--------------\n", 16);
		// ------------------------------ end for debug

		buflen = namelen + 2*sizeof(unsigned int);
		c = buffer = ngx_pnalloc(r->pool, buflen);
		if(buffer == NULL) {
			return NGX_HTTP_INTERNAL_SERVER_ERROR;
		}	
		// 鏂囦欢鍚嶉暱搴�:4瀛楄妭
		for(j=0; j<sizeof(unsigned int); j++) {
			*c++ = namelen & 0x000000ff;
			namelen >>= 8;
		}

		c = ngx_cpymem(c, filename->data+root->len, filename->len-root->len);

		if(openf != NGX_OK) {
			size = 0;
		} else {
			size = (unsigned int)of->size;
		}
		for(j=0; j<sizeof(unsigned int); j++) {
			*c++ = size & 0x000000ff;
			size >>= 8;
		}

		b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
		if (b == NULL) {
			return NGX_HTTP_INTERNAL_SERVER_ERROR;
		}
		b->pos = buffer;
		b->last = buffer + buflen;
		b->memory = 1;

		ctx->length += buflen;//

		if(fid & FIRST_FILE) {
			out->buf = b;
			out->next = NULL;

		} else {

			cl = ngx_alloc_chain_link(r->pool);
			if (cl == NULL) {
				return NGX_HTTP_INTERNAL_SERVER_ERROR;
			}

			cl->buf = b;
			cl->next = NULL;

			for(ch = out; ch->next; ch = ch->next);
			ch->next = cl;
		}
	}

	if(openf == NGX_OK) {
		b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
		if (b == NULL) {
			return NGX_HTTP_INTERNAL_SERVER_ERROR;
		}

		b->file = ngx_pcalloc(r->pool, sizeof(ngx_file_t));
		if (b->file == NULL) {
			return NGX_HTTP_INTERNAL_SERVER_ERROR;
		}

		b->file_pos = 0;
		b->file_last = of->size;
		b->in_file = b->file_last ? 1 : 0;
		b->file->fd = of->fd;
		b->file->name = *filename;
		b->file->log = r->connection->log;
		b->file->directio = of->is_directio;

		ctx->length += of->size;//

		if (!clcf->with_file_size && (fid & FIRST_FILE)) {
			out->buf = b;
			out->next = NULL;

		} else {
			cl = ngx_alloc_chain_link(r->pool);
			if (cl == NULL) {
				return NGX_HTTP_INTERNAL_SERVER_ERROR;
			}

			cl->buf = b;
			cl->next = NULL;

			for(ch = out; ch->next; ch = ch->next);
			ch->next = cl;
		}
	}	

	// 璁剧疆鏈€鍚庣殑buffer鏍囧織浣�
	if(fid & LAST_FILE) {
        b->last_in_chain = 1;
        b->last_buf = 1;
		return NGX_OK;
	}

	if (clcf->delimiter.len == 0) {
		return NGX_OK;
	}

	b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
	if (b == NULL) {
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	b->pos = clcf->delimiter.data;
	b->last = b->pos + clcf->delimiter.len;
	b->memory = 1;
	ctx->length += clcf->delimiter.len;

	cl = ngx_alloc_chain_link(r->pool);
	if (cl == NULL) {
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	cl->buf = b;
	cl->next = NULL;

	for(ch = out; ch->next; ch = ch->next);
	ch->next = cl;

	return NGX_OK;
}

static ngx_inline u_char *
ngx_strrchr(u_char *p, u_char *last, u_char c)
{
    while (p > last) {

        if (*p == c) {
            return p;
        }

        p--;
    }

    return NULL;
}

static ngx_int_t
ngx_http_get_file_subrequest(ngx_http_request_t *r, ngx_str_t *fullname, ngx_int_t id)
{
	// /data/wwwroot/ssi/a.shtml
	// |____________| 			 --- root(/data/wwwroot/)
	//               |__| 		 --- path(ssi/)
	//                   |_____| --- name(a.shtml)

	ngx_http_file_wrapper_t *file_r = ngx_pcalloc(r->pool, sizeof(ngx_http_file_wrapper_t));

	file_r->id = id;//


	ngx_http_concat_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_concat_module);

	u_char *s = fullname->data + ctx->root->len;
	u_char *e = ngx_strrchr(fullname->data+fullname->len-1, s, '/');
	file_r->path.data = s;
	if(e == NULL) {
		e = s-1;
		file_r->path.len = 0;
	} else {
		file_r->path.len = e - s + 1;
	}


	ngx_file_t *file = ngx_pcalloc(r->pool, sizeof(ngx_file_t));
	file->name.data = e + 1;
	file->name.len = fullname->data + fullname->len - file->name.data;
	file->log = r->connection->log;

	file_r->file = file;


	ngx_http_post_subrequest_t *psr = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
	if(psr == NULL) {
		return NGX_ERROR;
	}
	psr->handler = subrequest_post_handler; //
	psr->data = file_r;

	ngx_str_t prefix = ngx_string("/ssi/");
	ngx_str_t uri;
	uri.len = prefix.len + file_r->path.len + file_r->file->name.len;
	uri.data = ngx_palloc(r->pool, uri.len);
	ngx_snprintf(uri.data, uri.len, "%V%V%V", &prefix, &file_r->path, &file_r->file->name);

	ngx_http_request_t *sr;

	ngx_int_t rc = ngx_http_subrequest(r, &uri, NULL, &sr, psr, NGX_HTTP_SUBREQUEST_IN_MEMORY);
	if(rc != NGX_OK) {
		return NGX_ERROR;
	}

	return NGX_OK;
}

static ngx_int_t 
subrequest_post_handler(ngx_http_request_t *r, void *data, ngx_int_t rc)
{
	ngx_http_request_t *pr = r->parent;

	ngx_http_concat_ctx_t *ctx = ngx_http_get_module_ctx(pr, ngx_http_concat_module);
	if(ctx->subreqs <= 0) {
		ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "\teeeeeeeeee\tSubreqs is %d", ctx->subreqs);
		return NGX_OK;
	}
	ctx->subreqs--;

	ngx_http_file_wrapper_t *file_r = (ngx_http_file_wrapper_t *)data;

	ngx_file_t *file = file_r->file;
	u_char fullname[256];
	ngx_memzero(fullname, 256);
	ngx_snprintf(fullname, 256, "%V%V%V", ctx->root, &file_r->path, &file->name);

	ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
				  "\t2222222222\tsubrequest_post_handler() "
				  "fullname: \"%s\" path: %V Subreqs[%d]", 
				  fullname, &file_r->path, ctx->subreqs);

	pr->headers_out.status = r->headers_out.status;
	if(r->headers_out.status == NGX_HTTP_OK) {


		ngx_buf_t *buf = &r->upstream->buffer;


		ngx_subid_node_t *node = ctx->subids->elts;
		ngx_uint_t i = 0, count = 0;
		for(; i < ctx->subids->nelts; i++) {
	
			if(node[i].done == 0 && node[i].subid < file_r->id) { // 璁℃暟
				count++;
			} else {
				if(node[i].subid == file_r->id) {
					node[i].done = 1;
					break;
				}
			}
		}

		ngx_chain_t *out = ctx->out;
		ngx_int_t pos = 2*file_r->id-count;
		while(pos-->0 && out->next) {
			out = out->next;
		}

		ngx_int_t size = buf->last - buf->pos;// + 1;
		ngx_int_t sz = size;
		ngx_uint_t j;
		u_char *c = out->buf->last - sizeof(unsigned int);// + 1;
		for(j=0; j<sizeof(unsigned int); j++) {
			*c++ = sz & 0x000000ff;
			sz >>= 8;
		}
		ngx_chain_t *ch = ngx_pcalloc(pr->pool, sizeof(ngx_chain_t));
		if(ch == NULL) {
			return NGX_ERROR;
		}
		ngx_buf_t *b = ngx_pcalloc(pr->pool, sizeof(ngx_buf_t));
		if(b == NULL) {
			return NGX_ERROR;
		}
		
		c = ngx_pnalloc(pr->pool, size);
		if(c == NULL) {
			return NGX_ERROR;
		}
		ngx_memcpy(c, buf->pos, size);
		
		b->memory = 1;
		b->pos = c;
		b->last = c + size;

		ch->buf = b;

		if(out->next == NULL) {
			out->buf->last_in_chain = 0;
			out->buf->last_buf = 0;

			b->last_in_chain = 1;
			b->last_buf = 1;
		}
		ch->next = out->next;
		out->next = ch;	

		ctx->length += size;

		if( file->fd <= 0 ) {

			if(file_r->path.len > 0) {
				u_char fullpath[256];
				ngx_memzero(fullpath, 256);
				ngx_snprintf(fullpath, 256, "%V%V", ctx->root, &file_r->path);
				if(ngx_create_full_path(fullpath, 0777)) {
					ngx_log_error(NGX_LOG_ERR, r->connection->log, ngx_errno, 
							      "ngx_create_full_path() failed");
					return NGX_ERROR;
				}
			}

			file->fd = ngx_open_file(fullname, NGX_FILE_RDWR|NGX_FILE_CREATE_OR_OPEN|NGX_FILE_NONBLOCK, 
									 NGX_FILE_OPEN, 0777);
			if( file->fd <= 0 ) {
				ngx_log_error(NGX_LOG_ERR, r->connection->log, ngx_errno, "ngx_open_file() failed");
				return NGX_ERROR;
			}
		}


		ngx_err_t  err = ngx_trylock_fd(file->fd);
		if (err != 0) {
			ngx_log_error(NGX_LOG_ERR, r->connection->log, ngx_errno, "ngx_trylock_fd() failed");
			ngx_close_file(file->fd);
			file->fd = 0;
			return NGX_ERROR;
		}


        ngx_write_file(file, buf->pos, (size_t)(buf->last-buf->pos), file->sys_offset);

		err = ngx_unlock_fd(file->fd);
		if (err != 0) {
			ngx_log_error(NGX_LOG_ERR, r->connection->log, ngx_errno, "ngx_unlock_fd() failed");
			ngx_close_file(file->fd);
			file->fd = 0;
			return NGX_ERROR;
		}

		ngx_close_file(file->fd);
		file->fd = 0;
	}


	pr->write_event_handler = ngx_http_post_handler;
	
	return NGX_OK;
}

static void
ngx_http_post_handler(ngx_http_request_t *r) 
{
	ngx_http_concat_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_concat_module);

	ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "\t3333333333\tngx_http_post_handler()\tSubreqs[%d]", ctx->subreqs);

	if(ctx->subreqs > 0) {
		return;
	}

	r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = ctx->length;
	r->connection->buffered |= NGX_HTTP_WRITE_BUFFERED;
    r->headers_out.last_modified_time = ctx->last_modified;

	if(ctx->out->buf == NULL) {
		r->header_only = 1;
	}


    ngx_int_t rc = ngx_http_send_header(r);
    rc = ngx_http_output_filter(r, ctx->out);

	ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "\t----------\tngx_http_post_handler()\tSubreqs[%d]", ctx->subreqs);
	ngx_http_finalize_request(r, rc);
}

static ngx_int_t
ngx_http_concat_add_path(ngx_http_request_t *r, ngx_array_t *uris,
    size_t max, ngx_str_t *path, u_char *p, u_char *v)
{
    u_char     *d;
    ngx_str_t  *uri, args;
    ngx_uint_t  flags;

    if (p == v) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "client sent zero concat filename");
        return NGX_HTTP_BAD_REQUEST;
    }

    if (uris->nelts >= max) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "client sent too many concat filenames");
        return NGX_HTTP_BAD_REQUEST;
    }

    uri = ngx_array_push(uris);
    if (uri == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    uri->len = path->len + p - v;
    uri->data = ngx_pnalloc(r->pool, uri->len + 1);  /* + '\0' */
    if (uri->data == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    d = ngx_cpymem(uri->data, path->data, path->len);
    d = ngx_cpymem(d, v, p - v);
    *d = '\0';

    args.len = 0;
    args.data = NULL;
    flags = NGX_HTTP_LOG_UNSAFE;

    if (ngx_http_parse_unsafe_uri(r, uri, &args, &flags) != NGX_OK) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
					  "ngx_http_parse_unsafe_uri() returns ERROR");
        return NGX_HTTP_BAD_REQUEST;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http concat add file: \"%s\"", uri->data);

    return NGX_OK;
}


static void *
ngx_http_concat_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_concat_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_concat_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->types = { NULL };
     *     conf->types_keys = NULL;
     */

    conf->enable = NGX_CONF_UNSET;
    conf->ignore_file_error = NGX_CONF_UNSET;
    conf->max_files = NGX_CONF_UNSET_UINT;
    conf->unique = NGX_CONF_UNSET;
	conf->with_file_size = NGX_CONF_UNSET;

    return conf;
}


static char *
ngx_http_concat_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_concat_loc_conf_t *prev = parent;
    ngx_http_concat_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    ngx_conf_merge_str_value(conf->delimiter, prev->delimiter, "");
    ngx_conf_merge_value(conf->ignore_file_error, prev->ignore_file_error, 0);
    ngx_conf_merge_uint_value(conf->max_files, prev->max_files, 10);
    ngx_conf_merge_value(conf->unique, prev->unique, 1);
    ngx_conf_merge_value(conf->with_file_size, prev->with_file_size, 0);

    if (ngx_http_merge_types(cf, &conf->types_keys, &conf->types,
                             &prev->types_keys, &prev->types,
                             ngx_http_concat_default_types)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_concat_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt       *h;
    ngx_http_core_main_conf_t *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_concat_handler;

    return NGX_OK;
}
