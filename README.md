# ssi_remote
Page files and fragments are stored on different servers.

In traditional nginx, a subrequest is generated for each fragment, and page files and fragments are stored on same server. With this module, page files and fragments are stored on different servers, and the fragments are fetched in one subrequest.

There are two part in this server, one is page server, the other is fragment server. Client send request to page server, page server fetches fragments from fragment server, the complete page is returned after assembling.

The fragment server is modified by concat module from alibaba(https://github.com/alibaba/nginx-http-concat). If the fragment asked is not stored locally, a subrequest is generaged to fetch the very fragemnt from somewhere else.

Only "include" command is supported. There may be some bugs, since coders are rookies, LOL~
