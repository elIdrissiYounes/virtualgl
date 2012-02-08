/* Copyright (C)2004 Landmark Graphics Corporation
 * Copyright (C)2005, 2006 Sun Microsystems, Inc.
 * Copyright (C)2009, 2011 D. R. Commander
 *
 * This library is free software and may be redistributed and/or modified under
 * the terms of the wxWindows Library License, Version 3.1 or (at your option)
 * any later version.  The full license is in the LICENSE.txt file included
 * with this distribution.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * wxWindows Library License for more details.
 */

// Interposed GLX functions

#include "faker-sym.h"
#include <limits.h>


// Applications will sometimes use X11 functions to obtain a list of 2D X
// server visuals, then pass one of those visuals to glXCreateContext(),
// glXGetConfig(), etc.  Since the visual didn't come from glXChooseVisual(),
// VGL has no idea what properties the app is looking for, so if no 3D X server
// FB config is already hashed to the visual, we have to create one using
// default attributes.

#define testattrib(attrib, index, min, max) {  \
	if(!strcmp(argv[i], #attrib) && i<argc-1) {  \
		int temp=atoi(argv[++i]);  \
		if(temp>=min && temp<=max) {  \
			attribs[index++]=attrib;  \
			attribs[index++]=temp;  \
		}  \
	}  \
}

static GLXFBConfig _MatchConfig(Display *dpy, XVisualInfo *vis)
{
	GLXFBConfig c=0, *configs=NULL;  int n=0;
	if(!dpy || !vis) return 0;
	if(!(c=vish.getpbconfig(dpy, vis))&& !(c=vish.mostrecentcfg(dpy, vis)))
	{
		// Punt.  We can't figure out where the visual came from
		int default_attribs[]={GLX_DOUBLEBUFFER, 1, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
			GLX_BLUE_SIZE, 8, GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_STEREO, 0,
			GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT, GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
			GLX_DEPTH_SIZE, 1, None};
		int attribs[256];

		memset(attribs, 0, sizeof(attribs));
		memcpy(attribs, default_attribs, sizeof(default_attribs));
		if(__vglClientVisualAttrib(dpy, DefaultScreen(dpy), vis->visualid, GLX_STEREO))
			attribs[11]=1;

		// Allow the default FB config attribs to be manually specified.  This is
		// necessary to support apps that implement their own visual selection
		// mechanisms.  Since those apps don't use glXChooseVisual(), VirtualGL has
		// no idea what 3D visual attributes they need, and thus it is necessary
		// to give it a hint using this environment variable.
		char *env=getenv("VGL_DEFAULTFBCONFIG");
		char *argv[512];  int argc=0;
		if(env && strlen(env)>0)
		{
			char *arg=strtok(env, " \t,");
			while(arg && argc<512)
			{
				argv[argc]=arg;  argc++;
				arg=strtok(NULL, " \t,");
			}
		}
		for(int i=0, j=18; i<argc && j<256; i++)
		{
			int index;
			index=2;
			testattrib(GLX_RED_SIZE, index, 0, INT_MAX);
			index=4;
			testattrib(GLX_GREEN_SIZE, index, 0, INT_MAX);
			index=6;
			testattrib(GLX_BLUE_SIZE, index, 0, INT_MAX);
			index=16;
			testattrib(GLX_DEPTH_SIZE, index, 0, INT_MAX);
			testattrib(GLX_ALPHA_SIZE, j, 0, INT_MAX);
			testattrib(GLX_STENCIL_SIZE, j, 0, INT_MAX);
			testattrib(GLX_AUX_BUFFERS, j, 0, INT_MAX);
			testattrib(GLX_ACCUM_RED_SIZE, j, 0, INT_MAX);
			testattrib(GLX_ACCUM_GREEN_SIZE, j, 0, INT_MAX);
			testattrib(GLX_ACCUM_BLUE_SIZE, j, 0, INT_MAX);
			testattrib(GLX_ACCUM_ALPHA_SIZE, j, 0, INT_MAX);
			testattrib(GLX_SAMPLE_BUFFERS, j, 0, INT_MAX);
			testattrib(GLX_SAMPLES, j, 0, INT_MAX);
		}

		configs=glXChooseFBConfig(_localdpy, DefaultScreen(_localdpy), attribs, &n);
		if((!configs || n<1) && attribs[11])
		{
			attribs[11]=0;
			configs=glXChooseFBConfig(_localdpy, DefaultScreen(_localdpy), attribs, &n);
		}
		if((!configs || n<1) && attribs[1])
		{
			attribs[1]=0;
			configs=glXChooseFBConfig(_localdpy, DefaultScreen(_localdpy), attribs, &n);
		}
		if(!configs || n<1) return 0;
		c=configs[0];
		XFree(configs);
		if(c)
		{
			vish.add(dpy, vis, c);
			cfgh.add(dpy, c, vis->visualid);
		}
	}
	return c;
}


// Return the 2D X server visual that was previously hashed to 'config', or
// find and return an appropriate 2D X server visual otherwise.

static VisualID _MatchVisual(Display *dpy, GLXFBConfig config)
{
	VisualID vid=0;
	if(!dpy || !config) return 0;
	int screen=DefaultScreen(dpy);
	if(!(vid=cfgh.getvisual(dpy, config)))
	{
		vid=__vglMatchVisual(dpy, screen, __vglConfigDepth(config),
				__vglConfigClass(config),
				0,
				__vglServerVisualAttrib(config, GLX_STEREO),
				0);
		if(!vid) 
			vid=__vglMatchVisual(dpy, screen, 24, TrueColor, 0, 0, 0);
	}
	if(vid) cfgh.add(dpy, config, vid);
	return vid;
}


// If GLXDrawable is a window ID, then return the ID for its corresponding
// Pbuffer (if applicable.)

GLXDrawable ServerDrawable(Display *dpy, GLXDrawable draw)
{
	pbwin *pbw=NULL;
	if(winh.findpb(dpy, draw, pbw)) return pbw->getglxdrawable();
	else return draw;
}


extern "C" {

// Return a set of Pbuffer-compatible FB configs from the 3D X server that
// contain the desired GLX attributes.

GLXFBConfig *glXChooseFBConfig(Display *dpy, int screen,
	const int *attrib_list, int *nelements)
{
	GLXFBConfig *configs=NULL;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy))
	{
			opentrace(_glXChooseFBConfig);  prargd(dpy);  prargi(screen);
			prargal13(attrib_list);  starttrace();

		configs=_glXChooseFBConfig(dpy, screen, attrib_list, nelements);

			stoptrace();  if(configs) {prargc(configs[0]);}
			if(nelements) {prargi(*nelements);}	 closetrace();

		return configs;
	}
	////////////////////

		opentrace(glXChooseFBConfig);  prargd(dpy);  prargi(screen);
		prargal13(attrib_list);  starttrace();

	// If 'attrib_list' specifies properties for transparent overlay rendering,
	// then hand off to the 2D X server.
	if(attrib_list)
	{
		bool overlayreq=false;
		for(int i=0; attrib_list[i]!=None && i<=254; i+=2)
		{
			if(attrib_list[i]==GLX_LEVEL && attrib_list[i+1]==1)
				overlayreq=true;
		}
		if(overlayreq)
		{
			int dummy;
			if(!_XQueryExtension(dpy, "GLX", &dummy, &dummy, &dummy))
				configs=NULL;
			else configs=_glXChooseFBConfig(dpy, screen, attrib_list, nelements);
			if(configs && nelements && *nelements)
			{
				for(int i=0; i<*nelements; i++) rcfgh.add(dpy, configs[i]);
			}
			stoptrace();  if(configs) {prargc(configs[0]);}
			if(nelements) {prargi(*nelements);}	 closetrace();
			return configs;
		}
	}

	int depth=24, c_class=TrueColor, level=0, stereo=0, trans=0, temp;
	if(!nelements) nelements=&temp;
	*nelements=0;

	// No attributes specified.  Return all FB configs.
	if(!attrib_list)
		configs=_glXChooseFBConfig(_localdpy, DefaultScreen(_localdpy),
			attrib_list, nelements);

	// Modify the attributes so that only FB configs appropriate for Pbuffer
	// rendering are considered.
	else configs=__vglConfigsFromVisAttribs(attrib_list, depth, c_class, level,
		stereo, trans, *nelements, true);

	if(configs && *nelements)
	{
		// Get a matching visual from the 2D X server and hash it to every FB
		// config we just obtained.
		VisualID vid=__vglMatchVisual(dpy, screen, depth, c_class, level, stereo,
			trans);
		if(vid) for(int i=0; i<*nelements; i++) cfgh.add(dpy, configs[i], vid);
		else {XFree(configs);  return NULL;}
	}

		stoptrace();  if(configs) {prargc(configs[0]);}
		if(nelements) {prargi(*nelements);}	 closetrace();

	CATCH();
	return configs;
}

#ifdef SUNOGL
GLXFBConfigSGIX *glXChooseFBConfigSGIX (Display *dpy, int screen,
	const int *attrib_list, int *nelements)
#else
GLXFBConfigSGIX *glXChooseFBConfigSGIX (Display *dpy, int screen,
	int *attrib_list, int *nelements)
#endif
{
	return glXChooseFBConfig(dpy, screen, attrib_list, nelements);
}


// Obtain a Pbuffer-compatible 3D X server FB config that has the desired set
// of attributes, match it to an appropriate 2D X server visual, hash the two,
// and return the visual.

XVisualInfo *glXChooseVisual(Display *dpy, int screen, int *attrib_list)
{
	XVisualInfo *v=NULL;
	static bool alreadywarned=false;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXChooseVisual(dpy, screen, attrib_list);
	////////////////////

		opentrace(glXChooseVisual);  prargd(dpy);  prargi(screen);
		prargal11(attrib_list);  starttrace();

	// If 'attrib_list' specifies properties for transparent overlay rendering,
	// then hand off to the 2D X server.
	if(attrib_list)
	{
		bool overlayreq=false;
		for(int i=0; attrib_list[i]!=None && i<=254; i++)
		{
			if(attrib_list[i]==GLX_DOUBLEBUFFER || attrib_list[i]==GLX_RGBA
				|| attrib_list[i]==GLX_STEREO || attrib_list[i]==GLX_USE_GL)
				continue;
			else if(attrib_list[i]==GLX_LEVEL && attrib_list[i+1]==1)
			{
				overlayreq=true;  i++;
			}
			else i++;
		}
		if(overlayreq)
		{
			int dummy;
			if(!_XQueryExtension(dpy, "GLX", &dummy, &dummy, &dummy))
				v=NULL;
			else v=_glXChooseVisual(dpy, screen, attrib_list);
			stoptrace();  prargv(v);  closetrace();
			return v;
		}
	}

	// Use the specified set of GLX attributes to obtain a Pbuffer-compatible FB
	// config on the 3D X server
	GLXFBConfig *configs=NULL, c=0, cprev;  int n=0;
	if(!dpy || !attrib_list) return NULL;
	int depth=24, c_class=TrueColor, level=0, stereo=0, trans=0;
	if(!(configs=__vglConfigsFromVisAttribs(attrib_list, depth, c_class,
		level, stereo, trans, n)) || n<1)
	{
		if(!alreadywarned && fconfig.verbose)
		{
			alreadywarned=true;
			rrout.println("[VGL] WARNING: VirtualGL attempted and failed to obtain a Pbuffer-enabled");
			rrout.println("[VGL]    24-bit visual on the 3D X server %s.  This is normal if", fconfig.localdpystring);
			rrout.println("[VGL]    the 3D application is probing for visuals with certain capabilities,");
			rrout.println("[VGL]    but if the app fails to start, then make sure that the 3D X server is");
			rrout.println("[VGL]    configured for 24-bit color and has accelerated 3D drivers installed.");
		}
		return NULL;
	}
	c=configs[0];
	XFree(configs);

	// Find an appropriate matching visual on the 2D X server.
	VisualID vid=__vglMatchVisual(dpy, screen, depth, c_class, level, stereo, trans);
	if(!vid) return NULL;
	v=__vglVisualFromVisualID(dpy, screen, vid);
	if(!v) return NULL;

	if((cprev=vish.getpbconfig(dpy, v)) && _FBCID(c) != _FBCID(cprev)
		&& fconfig.trace)
		rrout.println("[VGL] WARNING: Visual 0x%.2x was previously mapped to FB config 0x%.2x and is now mapped to 0x%.2x\n",
			v->visualid, _FBCID(cprev), _FBCID(c));

	// Hash the FB config and the visual so that we can look up the FB config
	// whenever the app subsequently passes the visual to glXCreateContext() or
	// other functions.
	vish.add(dpy, v, c);

		stoptrace();  prargv(v);  prargc(c);  closetrace();

	CATCH();
	return v;
}


// If src or dst is an overlay context, hand off to the 2D X server.
// Otherwise, hand off to the 3D X server without modification.

#ifdef SUNOGL
void glXCopyContext(Display *dpy, GLXContext src, GLXContext dst, unsigned int mask)
#else
void glXCopyContext(Display *dpy, GLXContext src, GLXContext dst, unsigned long mask)
#endif
{
	TRY();
	bool srcoverlay=false, dstoverlay=false;
	if(ctxh.isoverlay(src)) srcoverlay=true;
	if(ctxh.isoverlay(dst)) dstoverlay=true;
	if(srcoverlay && dstoverlay)
		{_glXCopyContext(dpy, src, dst, mask);  return;}
	else if(srcoverlay!=dstoverlay)
		_throw("glXCopyContext() cannot copy between overlay and non-overlay contexts");
	_glXCopyContext(_localdpy, src, dst, mask);
	CATCH();
}


// Create a Pbuffer-compatible GLX context on the 3D X server.

GLXContext glXCreateContext(Display *dpy, XVisualInfo *vis,
	GLXContext share_list, Bool direct)
{
	GLXContext ctx=0;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXCreateContext(dpy, vis, share_list, direct);
	////////////////////

		opentrace(glXCreateContext);  prargd(dpy);  prargv(vis);
		prargx(share_list);  prargi(direct);  starttrace();

	if(!fconfig.allowindirect) direct=True;

	// If 'vis' is an overlay visual, hand off to the 2D X server.
	if(vis)
	{
		int level=__vglClientVisualAttrib(dpy, DefaultScreen(dpy), vis->visualid,
			GLX_LEVEL);
		int trans=(__vglClientVisualAttrib(dpy, DefaultScreen(dpy), vis->visualid,
			GLX_TRANSPARENT_TYPE)==GLX_TRANSPARENT_INDEX);
		if(level && trans)
		{
			int dummy;
			if(!_XQueryExtension(dpy, "GLX", &dummy, &dummy, &dummy))
				ctx=NULL;
			else ctx=_glXCreateContext(dpy, vis, share_list, direct);
			if(ctx) ctxh.add(ctx, (GLXFBConfig)-1);
			stoptrace();  prargx(ctx);  closetrace();
			return ctx;
		}
	}

	// If 'vis' was obtained through a previous call to glXChooseVisual(), find
	// the corresponding FB config in the hash.  Otherwise, we have to fall back
	// to using a default FB config returned from _MatchConfig().
	GLXFBConfig c;
	if(!(c=_MatchConfig(dpy, vis)))
		_throw("Could not obtain Pbuffer-capable RGB visual on the server");
	ctx=_glXCreateNewContext(_localdpy, c, GLX_RGBA_TYPE, share_list, direct);
	if(ctx)
	{
		if(!_glXIsDirect(_localdpy, ctx) && direct)
		{
			rrout.println("[VGL] WARNING: The OpenGL rendering context obtained on X display");
			rrout.println("[VGL]    %s is indirect, which may cause performance to suffer.",
				DisplayString(_localdpy));
			rrout.println("[VGL]    If %s is a local X display, then the framebuffer device",
				DisplayString(_localdpy));
			rrout.println("[VGL]    permissions may be set incorrectly.");
		}
		// Hash the FB config to the context so we can use it in subsequent calls
		// to glXMake[Context]Current().
		ctxh.add(ctx, c);
	}

		stoptrace();  prargc(c);  prargx(ctx);  closetrace();

	CATCH();
	return ctx;
}


GLXContext glXCreateContextAttribsARB(Display *dpy, GLXFBConfig config,
	GLXContext share_context, Bool direct, const int *attribs)
{
	GLXContext ctx=0;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy))
		return _glXCreateContextAttribsARB(dpy, config, share_context, direct,
			attribs);
	////////////////////

		opentrace(glXCreateContextAttribsARB);  prargd(dpy);  prargc(config);
		prargx(share_context);  prargi(direct);  prargal13(attribs);
		starttrace();

	if(!fconfig.allowindirect) direct=True;

	// Overlay config.  Hand off to 2D X server.
	if(rcfgh.isoverlay(dpy, config))
	{
		ctx=_glXCreateContextAttribsARB(dpy, config, share_context, direct,
			attribs);
		if(ctx) ctxh.add(ctx, (GLXFBConfig)-1);
		stoptrace();  prargx(ctx);  closetrace();
		return ctx;
	}

	if(attribs)
	{
		// Color index rendering is handled behind the scenes using the red
		// channel of an RGB Pbuffer, so VirtualGL always uses RGBA contexts.
		for(int i=0; attribs[i]!=None && i<=254; i+=2)
		{
			if(attribs[i]==GLX_RENDER_TYPE) ((int *)attribs)[i+1]=GLX_RGBA_TYPE;
		}
	}

	ctx=_glXCreateContextAttribsARB(_localdpy, config, share_context, direct,
		attribs);
	if(ctx)
	{
		if(!_glXIsDirect(_localdpy, ctx) && direct)
		{
			rrout.println("[VGL] WARNING: The OpenGL rendering context obtained on X display");
			rrout.println("[VGL]    %s is indirect, which may cause performance to suffer.",
				DisplayString(_localdpy));
			rrout.println("[VGL]    If %s is a local X display, then the framebuffer device",
				DisplayString(_localdpy));
			rrout.println("[VGL]    permissions may be set incorrectly.");
		}
		ctxh.add(ctx, config);
	}

		stoptrace();  prargx(ctx);  closetrace();

	CATCH();
	return ctx;
}


GLXContext glXCreateNewContext(Display *dpy, GLXFBConfig config,
	int render_type, GLXContext share_list, Bool direct)
{
	GLXContext ctx=0;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy))
		return _glXCreateNewContext(dpy, config, render_type, share_list, direct);
	////////////////////

		opentrace(glXCreateNewContext);  prargd(dpy);  prargc(config);
		prargi(render_type);  prargx(share_list);  prargi(direct);  starttrace();

	if(!fconfig.allowindirect) direct=True;

	 // Overlay config.  Hand off to 2D X server.
	if(rcfgh.isoverlay(dpy, config))
	{
		ctx=_glXCreateNewContext(dpy, config, render_type, share_list, direct);
		if(ctx) ctxh.add(ctx, (GLXFBConfig)-1);
		stoptrace();  prargx(ctx);  closetrace();
		return ctx;
	}

	ctx=_glXCreateNewContext(_localdpy, config, GLX_RGBA_TYPE, share_list, direct);
	if(ctx)
	{
		if(!_glXIsDirect(_localdpy, ctx) && direct)
		{
			rrout.println("[VGL] WARNING: The OpenGL rendering context obtained on X display");
			rrout.println("[VGL]    %s is indirect, which may cause performance to suffer.",
				DisplayString(_localdpy));
			rrout.println("[VGL]    If %s is a local X display, then the framebuffer device",
				DisplayString(_localdpy));
			rrout.println("[VGL]    permissions may be set incorrectly.");
		}
		ctxh.add(ctx, config);
	}

		stoptrace();  prargx(ctx);  closetrace();

	CATCH();
	return ctx;
}

// On Linux, GLXFBConfigSGIX is typedef'd to GLXFBConfig

GLXContext glXCreateContextWithConfigSGIX(Display *dpy, GLXFBConfigSGIX config,
	int render_type, GLXContext share_list, Bool direct)
{
	return glXCreateNewContext(dpy, config, render_type, share_list, direct);
}


// We maintain a hash of GLX drawables to 2D X display handles for two reasons:
// (1) so we can determine in glXMake[Context]Current() whether or not a
// drawable ID is a window, a GLX drawable, or a window created in another
// application, and (2) so, if the application is rendering to a Pbuffer or
// a pixmap, we can return the correct 2D X display handle if it calls
// glXGetCurrentDisplay().

GLXPbuffer glXCreatePbuffer(Display *dpy, GLXFBConfig config,
	const int *attrib_list)
{
	GLXPbuffer pb=0;

		opentrace(glXCreatePbuffer);  prargd(dpy);  prargc(config);
		prargal13(attrib_list);  starttrace();

	pb=_glXCreatePbuffer(_localdpy, config, attrib_list);
	TRY();
	if(dpy && pb) glxdh.add(pb, dpy);
	CATCH();

		stoptrace();  prargx(pb);  closetrace();

	return pb;
}

GLXPbuffer glXCreateGLXPbufferSGIX(Display *dpy, GLXFBConfigSGIX config,
	unsigned int width, unsigned int height, int *attrib_list)
{
	int attribs[257], j=0;
	if(attrib_list)
	{
		for(int i=0; attrib_list[i]!=None && i<=254; i+=2)
		{
			attribs[j++]=attrib_list[i];  attribs[j++]=attrib_list[i+1];
		}
	}
	attribs[j++]=GLX_PBUFFER_WIDTH;  attribs[j++]=width;
	attribs[j++]=GLX_PBUFFER_HEIGHT;  attribs[j++]=height;
	attribs[j]=None;
	return glXCreatePbuffer(dpy, config, attribs);
}


// Pixmap rendering in VirtualGL is implemented using Pbuffers, so we create
// one, hash it to the Pixmap, and return the Pbuffer handle.

GLXPixmap glXCreateGLXPixmap(Display *dpy, XVisualInfo *vi, Pixmap pm)
{
	GLXPixmap drawable=0;
	TRY();
	GLXFBConfig c;

	// Prevent recursion
	if(!_isremote(dpy)) return _glXCreateGLXPixmap(dpy, vi, pm);
	////////////////////

		opentrace(glXCreateGLXPixmap);  prargd(dpy);  prargv(vi);  prargx(pm);
		starttrace();

	// Not sure whether a transparent pixmap has any meaning, but in any case,
	// we have to hand it off to the 2D X server.
	if(vi)
	{
		int level=__vglClientVisualAttrib(dpy, DefaultScreen(dpy), vi->visualid,
			GLX_LEVEL);
		int trans=(__vglClientVisualAttrib(dpy, DefaultScreen(dpy), vi->visualid,
			GLX_TRANSPARENT_TYPE)==GLX_TRANSPARENT_INDEX);
		if(level && trans)
		{
			int dummy;
			if(!_XQueryExtension(dpy, "GLX", &dummy, &dummy, &dummy))
				drawable=0;
			else drawable=_glXCreateGLXPixmap(dpy, vi, pm);
			stoptrace();  prargx(drawable);  closetrace();
			return drawable;
		}
	}

	Window root;  int x, y;  unsigned int w, h, bw, d;
	XGetGeometry(dpy, pm, &root, &x, &y, &w, &h, &bw, &d);
	if(!(c=_MatchConfig(dpy, vi)))
		_throw("Could not obtain Pbuffer-capable RGB visual on the server");
	pbpm *pbp=new pbpm(dpy, pm, vi->visual);
	if(pbp)
	{
		// Hash the pbpm instance to the Pixmap and also hash the 2D X display
		// handle to the GLXPixmap (which is really a Pbuffer.)
		pbp->init(w, h, c);
		pmh.add(dpy, pm, pbp);
		glxdh.add(pbp->getglxdrawable(), dpy);
		drawable=pbp->getglxdrawable();
	}

		stoptrace();  prargi(x);  prargi(y);  prargi(w);  prargi(h);
		prargi(d);  prargc(c);  prargx(drawable);  closetrace();

	CATCH();
	return drawable;
}


GLXPixmap glXCreatePixmap(Display *dpy, GLXFBConfig config, Pixmap pm,
	const int *attribs)
{
	GLXPixmap drawable=0;
	TRY();

	// Prevent recursion && handle overlay configs
	if(!_isremote(dpy) || rcfgh.isoverlay(dpy, config))
		return _glXCreatePixmap(dpy, config, pm, attribs);
	////////////////////

		opentrace(glXCreatePixmap);  prargd(dpy);  prargc(config);  prargx(pm);
		starttrace();

	Window root;  int x, y;  unsigned int w, h, bw, d;
	XGetGeometry(dpy, pm, &root, &x, &y, &w, &h, &bw, &d);

	VisualID vid=_MatchVisual(dpy, config);
	pbpm *pbp=NULL;
	if(vid)
	{
		XVisualInfo *v=__vglVisualFromVisualID(dpy, DefaultScreen(dpy), vid);
		if(v) pbp=new pbpm(dpy, pm, v->visual);
	}
	if(pbp)
	{
		pbp->init(w, h, config);
		pmh.add(dpy, pm, pbp);
		glxdh.add(pbp->getglxdrawable(), dpy);
		drawable=pbp->getglxdrawable();
	}

		stoptrace();  prargi(x);  prargi(y);  prargi(w);  prargi(h);
		prargi(d);  prargx(drawable);  closetrace();

	CATCH();
	return drawable;
}

GLXPixmap glXCreateGLXPixmapWithConfigSGIX(Display *dpy,
	GLXFBConfigSGIX config, Pixmap pixmap)
{
	return glXCreatePixmap(dpy, config, pixmap, NULL);
}


// Fake out the application into thinking it's getting a window drawable, but
// really it's getting a Pbuffer drawable.

GLXWindow glXCreateWindow(Display *dpy, GLXFBConfig config, Window win,
	const int *attrib_list)
{
	// Prevent recursion
	if(!_isremote(dpy)) return _glXCreateWindow(dpy, config, win, attrib_list);
	////////////////////

	TRY();

		opentrace(glXCreateWindow);  prargd(dpy);  prargc(config);  prargx(win);
		starttrace();

	pbwin *pbw=NULL;
	// Overlay config.  Hand off to 2D X server.
	if(rcfgh.isoverlay(dpy, config))
	{
		GLXWindow glxw=_glXCreateWindow(dpy, config, win, attrib_list);
		winh.setoverlay(dpy, glxw);
	}
	else
	{
		XSync(dpy, False);
		errifnot(pbw=winh.setpb(dpy, win, config));
	}

		stoptrace();  if(pbw) {prargx(pbw->getglxdrawable());}  closetrace();

	CATCH();
	return win;  // Make the client store the original window handle, which we
               // use to find the Pbuffer in the hash
}


// When the context is destroyed, remove it from the context-to-FB config hash.

void glXDestroyContext(Display* dpy, GLXContext ctx)
{
	TRY();

		opentrace(glXDestroyContext);  prargd(dpy);  prargx(ctx);  starttrace();

	if(ctxh.isoverlay(ctx))
	{
		_glXDestroyContext(dpy, ctx);
		stoptrace();  closetrace();
		return;
	}

	ctxh.remove(ctx);
	_glXDestroyContext(_localdpy, ctx);

		stoptrace();  closetrace();

	CATCH();
}


// When the Pbuffer is destroyed, remove it from the GLX drawable-to-2D display
// handle hash.

void glXDestroyPbuffer(Display *dpy, GLXPbuffer pbuf)
{
		opentrace(glXDestroyPbuffer);  prargd(dpy);  prargx(pbuf);  starttrace();

	_glXDestroyPbuffer(_localdpy, pbuf);
	TRY();
	if(pbuf) glxdh.remove(pbuf);
	CATCH();

		stoptrace();  closetrace();
}

void glXDestroyGLXPbufferSGIX(Display *dpy, GLXPbuffer pbuf)
{
	glXDestroyPbuffer(dpy, pbuf);
}


// Some applications will destroy the GLX pixmap handle but then try to use X11
// functions on the X11 pixmap handle, so we sync the contents of the Pbuffer
// with the real Pixmap before we delete the Pbuffer.

void glXDestroyGLXPixmap(Display *dpy, GLXPixmap pix)
{
	TRY();
	// Prevent recursion
	if(!_isremote(dpy)) {_glXDestroyGLXPixmap(dpy, pix);  return;}
	////////////////////

		opentrace(glXDestroyGLXPixmap);  prargd(dpy);  prargx(pix);  starttrace();

	pbpm *pbp=pmh.find(dpy, pix);
	if(pbp) pbp->readback();

	glxdh.remove(pix);
	pmh.remove(dpy, pix);

		stoptrace();  closetrace();

	CATCH();
}


void glXDestroyPixmap(Display *dpy, GLXPixmap pix)
{
	TRY();
	// Prevent recursion
	if(!_isremote(dpy)) {_glXDestroyPixmap(dpy, pix);  return;}
	////////////////////

		opentrace(glXDestroyPixmap);  prargd(dpy);  prargx(pix);  starttrace();

	pbpm *pbp=pmh.find(dpy, pix);
	if(pbp) pbp->readback();

	glxdh.remove(pix);
	pmh.remove(dpy, pix);

		stoptrace();  closetrace();

	CATCH();
}


// 'win' is really a Pbuffer ID, so the window hash matches it to the
// corresponding pbwin instance and shuts down that instance.

void glXDestroyWindow(Display *dpy, GLXWindow win)
{
	TRY();
	// Prevent recursion
	if(!_isremote(dpy)) {_glXDestroyWindow(dpy, win);  return;}
	////////////////////

		opentrace(glXDestroyWindow);  prargd(dpy);  prargx(win);  starttrace();

	if(winh.isoverlay(dpy, win)) _glXDestroyWindow(dpy, win);  
	winh.remove(dpy, win);

		stoptrace();  closetrace();

	CATCH();
}


// Hand off to the 2D X server (overlay rendering) or the 3D X server (opaque
// rendering) without modification.

void glXFreeContextEXT(Display *dpy, GLXContext ctx)
{
	if(ctxh.isoverlay(ctx)) {_glXFreeContextEXT(dpy, ctx);  return;}
	_glXFreeContextEXT(_localdpy, ctx);
}


// Since VirtualGL is effectively its own implementation of GLX, it needs to
// properly report the extensions and GLX version it supports.

static const char *glxextensions=
#ifdef SUNOGL
	"GLX_ARB_get_proc_address GLX_ARB_multisample GLX_EXT_visual_info GLX_EXT_visual_rating GLX_SGI_make_current_read GLX_SGIX_fbconfig GLX_SGIX_pbuffer GLX_SUN_get_transparent_index GLX_SUN_init_threads GLX_ARB_create_context";
#else
	"GLX_ARB_get_proc_address GLX_ARB_multisample GLX_EXT_visual_info GLX_EXT_visual_rating GLX_SGI_make_current_read GLX_SGIX_fbconfig GLX_SGIX_pbuffer GLX_SUN_get_transparent_index GLX_ARB_create_context";
#endif

const char *glXGetClientString(Display *dpy, int name)
{
	// If this is called internally to OpenGL, use the real function
	if(!_isremote(dpy)) return _glXGetClientString(dpy, name);
	////////////////////
	if(name==GLX_EXTENSIONS) return glxextensions;
	else if(name==GLX_VERSION) return "1.4";
	else if(name==GLX_VENDOR) return __APPNAME;
	else return NULL;
}


// For the specified 2D X server visual, return a property from the
// corresponding 3D X server FB config.

int glXGetConfig(Display *dpy, XVisualInfo *vis, int attrib, int *value)
{
	GLXFBConfig c;  int retval=0;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXGetConfig(dpy, vis, attrib, value);
	////////////////////

		opentrace(glXGetConfig);  prargd(dpy);  prargv(vis);  prargx(attrib);
		starttrace();

	if(!dpy || !vis || !value)
	{
		stoptrace()  prargx(value);  closetrace();
		return GLX_BAD_VALUE;
	}

	// If 'vis' is an overlay visual, hand off to the 2D X server.
	int level=__vglClientVisualAttrib(dpy, DefaultScreen(dpy), vis->visualid,
		GLX_LEVEL);
	int trans=(__vglClientVisualAttrib(dpy, DefaultScreen(dpy), vis->visualid,
		GLX_TRANSPARENT_TYPE)==GLX_TRANSPARENT_INDEX);
	if(level && trans && attrib!=GLX_LEVEL && attrib!=GLX_TRANSPARENT_TYPE)
	{
		int dummy;
		stoptrace();  if(value) {prargi(*value);}  closetrace();
		if(!_XQueryExtension(dpy, "GLX", &dummy, &dummy, &dummy))
			retval=GLX_NO_EXTENSION;
		else retval=_glXGetConfig(dpy, vis, attrib, value);
		return retval;
	}

	// If 'vis' was obtained through a previous call to glXChooseVisual(), find
	// the corresponding FB config in the hash.  Otherwise, we have to fall back
	// to using a default FB config returned from _MatchConfig().
	if(!(c=_MatchConfig(dpy, vis)))
		_throw("Could not obtain Pbuffer-capable RGB visual on the server");

	if(attrib==GLX_USE_GL)
	{
		if(vis->c_class==TrueColor || vis->c_class==PseudoColor) *value=1;
		else *value=0;
	}
	// Color index rendering really uses an RGB Pbuffer, so we have to fake out
	// the application if it is asking about RGBA properties.
	else if(vis->c_class==PseudoColor
		&& (attrib==GLX_RED_SIZE || attrib==GLX_GREEN_SIZE
			|| attrib==GLX_BLUE_SIZE || attrib==GLX_ALPHA_SIZE
			|| attrib==GLX_ACCUM_RED_SIZE || attrib==GLX_ACCUM_GREEN_SIZE
			|| attrib==GLX_ACCUM_BLUE_SIZE || attrib==GLX_ACCUM_ALPHA_SIZE))
		*value=0;
	// Transparent overlay visuals are actual 2D X server visuals, not dummy
	// visuals mapped to 3D X server FB configs, so we obtain their properties
	// from the 2D X server.
	else if(attrib==GLX_LEVEL || attrib==GLX_TRANSPARENT_TYPE
		|| attrib==GLX_TRANSPARENT_INDEX_VALUE
		|| attrib==GLX_TRANSPARENT_RED_VALUE
		|| attrib==GLX_TRANSPARENT_GREEN_VALUE
		|| attrib==GLX_TRANSPARENT_BLUE_VALUE
		|| attrib==GLX_TRANSPARENT_ALPHA_VALUE)
		*value=__vglClientVisualAttrib(dpy, vis->screen, vis->visualid, attrib);
	else if(attrib==GLX_RGBA)
	{
		if(vis->c_class==PseudoColor) *value=0;  else *value=1;
	}
	else if(attrib==GLX_STEREO)
		*value=__vglServerVisualAttrib(c, GLX_STEREO);		
	else if(attrib==GLX_X_VISUAL_TYPE)
	{
		if(vis->c_class==PseudoColor) *value=GLX_PSEUDO_COLOR;
		else *value=GLX_TRUE_COLOR;
	}
	else
	{
		if(attrib==GLX_BUFFER_SIZE && vis->c_class==PseudoColor
			&& __vglServerVisualAttrib(c, GLX_RENDER_TYPE)==GLX_RGBA_BIT)
			attrib=GLX_RED_SIZE;
		retval=_glXGetFBConfigAttrib(_localdpy, c, attrib, value);
	}

		stoptrace();  if(value) {prargi(*value);}  closetrace();

	CATCH();
	return retval;
}


// This returns the 2D X display handle associated with the current drawable,
// that is, the 2D X display handle passed to whatever function (such as
// XCreateWindow(), glXCreatePbuffer(), etc.) that was used to create the
// drawable.

Display *glXGetCurrentDisplay(void)
{
	Display *dpy=NULL;  pbwin *pbw=NULL;

	if(ctxh.overlaycurrent()) return _glXGetCurrentDisplay();

	TRY();

		opentrace(glXGetCurrentDisplay);  starttrace();

	GLXDrawable curdraw=_glXGetCurrentDrawable();
	if(winh.findpb(curdraw, pbw)) dpy=pbw->get2ddpy();
	else
	{
		if(curdraw) dpy=glxdh.getcurrentdpy(curdraw);
	}

		stoptrace();  prargd(dpy);  closetrace();

	CATCH();
	return dpy;
}


// As far as the application is concerned, it is rendering to a window and not
// a Pbuffer, so we must maintain that illusion and pass it back the window ID
// instead of the GLX drawable (Pbuffer) ID.

GLXDrawable glXGetCurrentDrawable(void)
{
	if(ctxh.overlaycurrent()) return _glXGetCurrentDrawable();

	pbwin *pbw=NULL;  GLXDrawable draw=_glXGetCurrentDrawable();

	TRY();

		opentrace(glXGetCurrentDrawable);  starttrace();

	if(winh.findpb(draw, pbw)) draw=pbw->getx11drawable();

		stoptrace();  prargx(draw);  closetrace();

	CATCH();
	return draw;
}

GLXDrawable glXGetCurrentReadDrawable(void)
{
	if(ctxh.overlaycurrent()) return _glXGetCurrentReadDrawable();

	pbwin *pbw=NULL;  GLXDrawable read=_glXGetCurrentReadDrawable();

	TRY();

		opentrace(glXGetCurrentReadDrawable);  starttrace();

	if(winh.findpb(read, pbw)) read=pbw->getx11drawable();

		stoptrace();  prargx(read);  closetrace();

	CATCH();
	return read;
}

GLXDrawable glXGetCurrentReadDrawableSGI(void)
{
	return glXGetCurrentReadDrawable();
}


// Return a property from the specified FB config.

int glXGetFBConfigAttrib(Display *dpy, GLXFBConfig config, int attribute,
	int *value)
{
	VisualID vid=0;  int retval=0;
	TRY();

	// Prevent recursion && handle overlay configs
	if((dpy && config) && (!_isremote(dpy) || rcfgh.isoverlay(dpy, config)))
		return _glXGetFBConfigAttrib(dpy, config, attribute, value);
	////////////////////

	int screen=dpy? DefaultScreen(dpy):0;

		opentrace(glXGetFBConfigAttrib);  prargd(dpy);  prargc(config);
		prargi(attribute);  starttrace();

	if(!dpy || !config || !value)
	{
		stoptrace();  prargx(value);  closetrace();
		return GLX_BAD_VALUE;
	}

	// Obtain the corresponding 2D X server visual from the hash, or find an
	// appropriate 2D X server visual for this FB config.
	if(!(vid=_MatchVisual(dpy, config)))
		throw rrerror("glXGetFBConfigAttrib", "Invalid FB config");

	// Color index rendering really uses an RGB Pbuffer, so we have to fake out
	// the application if it is asking about RGBA properties.
	int c_class=__vglVisualClass(dpy, screen, vid);
	if(c_class==PseudoColor
		&& (attribute==GLX_RED_SIZE
			|| attribute==GLX_GREEN_SIZE
			|| attribute==GLX_BLUE_SIZE || attribute==GLX_ALPHA_SIZE
			|| attribute==GLX_ACCUM_RED_SIZE || attribute==GLX_ACCUM_GREEN_SIZE
			|| attribute==GLX_ACCUM_BLUE_SIZE || attribute==GLX_ACCUM_ALPHA_SIZE))
		*value=0;
	// Transparent overlay FB configs are located on the 2D X server, not the 3D
	// X server.
	else if(attribute==GLX_LEVEL || attribute==GLX_TRANSPARENT_TYPE
		|| attribute==GLX_TRANSPARENT_INDEX_VALUE
		|| attribute==GLX_TRANSPARENT_RED_VALUE
		|| attribute==GLX_TRANSPARENT_GREEN_VALUE
		|| attribute==GLX_TRANSPARENT_BLUE_VALUE
		|| attribute==GLX_TRANSPARENT_ALPHA_VALUE)
		*value=__vglClientVisualAttrib(dpy, screen, vid, attribute);
	else if(attribute==GLX_RENDER_TYPE)
	{
		if(c_class==PseudoColor) *value=GLX_COLOR_INDEX_BIT;
		else *value=GLX_RGBA_BIT;
	}
	else if(attribute==GLX_STEREO)
	{
		*value= (__vglClientVisualAttrib(dpy, screen, vid, GLX_STEREO)
			&& __vglServerVisualAttrib(config, GLX_STEREO));
	}
	else if(attribute==GLX_X_VISUAL_TYPE)
	{
		if(c_class==PseudoColor) *value=GLX_PSEUDO_COLOR;
		else *value=GLX_TRUE_COLOR;
	}
	else if(attribute==GLX_VISUAL_ID)
		*value=vid;
	else if(attribute==GLX_DRAWABLE_TYPE)
	{
		*value=GLX_PIXMAP_BIT|GLX_PBUFFER_BIT|GLX_WINDOW_BIT;
	}
	else
	{
		if(attribute==GLX_BUFFER_SIZE && c_class==PseudoColor
			&& __vglServerVisualAttrib(config, GLX_RENDER_TYPE)==GLX_RGBA_BIT)
			attribute=GLX_RED_SIZE;
		if(attribute==GLX_CONFIG_CAVEAT)
		{
			int vistype=__vglServerVisualAttrib(config, GLX_X_VISUAL_TYPE);
			if(vistype!=GLX_TRUE_COLOR && vistype!=GLX_PSEUDO_COLOR)
			{
				*value=GLX_NON_CONFORMANT_CONFIG;
				return retval;
			}
		}
		retval=_glXGetFBConfigAttrib(_localdpy, config, attribute, value);
	}

		stoptrace();  if(value) {prargi(*value);}  closetrace();

	CATCH();
	return retval;
}

int glXGetFBConfigAttribSGIX(Display *dpy, GLXFBConfigSGIX config,
	int attribute, int *value_return)
{
	return glXGetFBConfigAttrib(dpy, config, attribute, value_return);
}


// See notes for _MatchConfig()

GLXFBConfigSGIX glXGetFBConfigFromVisualSGIX(Display *dpy, XVisualInfo *vis)
{
	return _MatchConfig(dpy, vis);
}


// Hand off to the 3D X server without modification

GLXFBConfig *glXGetFBConfigs(Display *dpy, int screen, int *nelements)
{
	return _glXGetFBConfigs(_localdpy, DefaultScreen(_localdpy), nelements);
}


// If an application uses glXGetProcAddressARB() to obtain the address of a
// function that we're interposing, we need to return the address of the
// interposed function.

#define checkfaked(f) if(!strcmp((char *)procName, #f)) {  \
	retval=(void (*)(void))f;  if(fconfig.trace) rrout.print("[INTERPOSED]");}
#ifdef SUNOGL
#define checkfakedidx(f) if(!strcmp((char *)procName, #f)) \
	retval=(void (*)(void))r_##f;
#else
#define checkfakedidx(f) checkfaked(f)
#endif

void (*glXGetProcAddressARB(const GLubyte *procName))(void)
{
	void (*retval)(void)=NULL;

	__vgl_fakerinit();

		opentrace(glXGetProcAddressARB);  prargs((char *)procName);  starttrace();

	if(procName)
	{
		checkfaked(glXGetProcAddressARB)
		checkfaked(glXGetProcAddress)

		checkfaked(glXChooseVisual)
		checkfaked(glXCopyContext)
		checkfaked(glXCreateContext)
		checkfaked(glXCreateGLXPixmap)
		checkfaked(glXDestroyContext)
		checkfaked(glXDestroyGLXPixmap)
		checkfaked(glXGetConfig)
		checkfaked(glXGetCurrentContext)
		checkfaked(glXGetCurrentDrawable)
		checkfaked(glXIsDirect)
		checkfaked(glXMakeCurrent);
		checkfaked(glXQueryExtension)
		checkfaked(glXQueryVersion)
		checkfaked(glXSwapBuffers)
		checkfaked(glXUseXFont)
		checkfaked(glXWaitGL)

		checkfaked(glXGetClientString)
		checkfaked(glXQueryServerString)
		checkfaked(glXQueryExtensionsString)

		checkfaked(glXChooseFBConfig)
		checkfaked(glXCreateNewContext)
		checkfaked(glXCreatePbuffer)
		checkfaked(glXCreatePixmap)
		checkfaked(glXCreateWindow)
		checkfaked(glXDestroyPbuffer)
		checkfaked(glXDestroyPixmap)
		checkfaked(glXDestroyWindow)
		checkfaked(glXGetCurrentDisplay)
		checkfaked(glXGetCurrentReadDrawable)
		checkfaked(glXGetCurrentReadDrawableSGI)
		checkfaked(glXGetFBConfigAttrib)
		checkfaked(glXGetFBConfigs)
		checkfaked(glXGetSelectedEvent)
		checkfaked(glXGetVisualFromFBConfig)
		checkfaked(glXMakeContextCurrent);
		checkfaked(glXMakeCurrentReadSGI)
		checkfaked(glXQueryContext)
		checkfaked(glXQueryDrawable)
		checkfaked(glXSelectEvent)

		checkfaked(glXFreeContextEXT)
		checkfaked(glXImportContextEXT)
		checkfaked(glXQueryContextInfoEXT)

		checkfaked(glXJoinSwapGroupNV)
		checkfaked(glXBindSwapBarrierNV)
		checkfaked(glXQuerySwapGroupNV)
		checkfaked(glXQueryMaxSwapGroupsNV)
		checkfaked(glXQueryFrameCountNV)
		checkfaked(glXResetFrameCountNV)

		checkfaked(glXChooseFBConfigSGIX)
		checkfaked(glXCreateContextWithConfigSGIX)
		checkfaked(glXCreateGLXPixmapWithConfigSGIX)
		checkfaked(glXCreateGLXPbufferSGIX)
		checkfaked(glXDestroyGLXPbufferSGIX)
		checkfaked(glXGetFBConfigAttribSGIX)
		checkfaked(glXGetFBConfigFromVisualSGIX)
		checkfaked(glXGetVisualFromFBConfigSGIX)
		checkfaked(glXQueryGLXPbufferSGIX)
		checkfaked(glXSelectEventSGIX)
		checkfaked(glXGetSelectedEventSGIX)
		checkfaked(glXSwapIntervalSGI)

		checkfaked(glXGetTransparentIndexSUN)

		checkfaked(glXCreateContextAttribsARB)

		checkfaked(glFinish)
		checkfaked(glFlush)
		checkfaked(glViewport)
		checkfaked(glDrawBuffer)
		checkfaked(glPopAttrib)
		checkfaked(glReadPixels)
		checkfaked(glDrawPixels)
		#ifdef SUNOGL
		checkfaked(glBegin)
		#endif
		checkfakedidx(glIndexd)
		checkfakedidx(glIndexf)
		checkfakedidx(glIndexi)
		checkfakedidx(glIndexs)
		checkfakedidx(glIndexub)
		checkfakedidx(glIndexdv)
		checkfakedidx(glIndexfv)
		checkfakedidx(glIndexiv)
		checkfakedidx(glIndexsv)
		checkfakedidx(glIndexubv)
		checkfaked(glClearIndex)
		checkfaked(glGetDoublev)
		checkfaked(glGetFloatv)
		checkfaked(glGetIntegerv)
		checkfaked(glPixelTransferf)
		checkfaked(glPixelTransferi)
	}
	if(!retval)
	{
		if(fconfig.trace) rrout.print("[passed through]");
		if(__glXGetProcAddressARB) retval=_glXGetProcAddressARB(procName);
		else if(__glXGetProcAddress) retval=_glXGetProcAddress(procName);
	}

		stoptrace();  closetrace();

	return retval;
}

void (*glXGetProcAddress(const GLubyte *procName))(void)
{
	return glXGetProcAddressARB(procName);
}


// Hand off to the 2D X server (overlay rendering) or the 3D X server (opaque
// rendering) without modification, except that, if 'draw' is not an overlay
// window, we replace it with its corresponding Pbuffer ID.

void glXGetSelectedEvent(Display *dpy, GLXDrawable draw,
	unsigned long *event_mask)
{
	if(winh.isoverlay(dpy, draw))
		return _glXGetSelectedEvent(dpy, draw, event_mask);

	_glXGetSelectedEvent(_localdpy, ServerDrawable(dpy, draw), event_mask);
}

void glXGetSelectedEventSGIX(Display *dpy, GLXDrawable drawable,
	unsigned long *mask)
{
	glXGetSelectedEvent(dpy, drawable, mask);
}


// Return the 2D X server visual that was hashed to the 3D X server FB config
// during a previous call to glXChooseFBConfig(), or pick out an appropriate
// visual and return it.

XVisualInfo *glXGetVisualFromFBConfig(Display *dpy, GLXFBConfig config)
{
	XVisualInfo *v=NULL;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXGetVisualFromFBConfig(dpy, config);
	////////////////////

		opentrace(glXGetVisualFromFBConfig);  prargd(dpy);  prargc(config);
		starttrace();

	// Overlay config.  Hand off to 2D X server.
	if(rcfgh.isoverlay(dpy, config))
	{
		v=_glXGetVisualFromFBConfig(dpy, config);
		stoptrace();  prargv(v);  closetrace();
		return v;
	}

	VisualID vid=0;
	if(!dpy || !config) return NULL;
	vid=_MatchVisual(dpy, config);
	if(!vid) return NULL;
	v=__vglVisualFromVisualID(dpy, DefaultScreen(dpy), vid);
	if(!v) return NULL;
	vish.add(dpy, v, config);

		stoptrace();  prargv(v);  closetrace();

	CATCH();
	return v;
}


XVisualInfo *glXGetVisualFromFBConfigSGIX(Display *dpy, GLXFBConfigSGIX config)
{
	return glXGetVisualFromFBConfig(dpy, config);
}



// Hand off to the 3D X server without modification

GLXContext glXImportContextEXT(Display *dpy, GLXContextID contextID)
{
	return _glXImportContextEXT(_localdpy, contextID);
}


// Hand off to the 2D X server (overlay rendering) or the 3D X server (opaque
// rendering) without modification

Bool glXIsDirect(Display *dpy, GLXContext ctx)
{
	Bool direct;

	if(ctxh.isoverlay(ctx)) return _glXIsDirect(dpy, ctx);

		opentrace(glXIsDirect);  prargd(dpy);  prargx(ctx);
		starttrace();

	direct=_glXIsDirect(_localdpy, ctx);

		stoptrace();  prargi(direct);  closetrace();

	return direct;
}


// See notes

Bool glXMakeCurrent(Display *dpy, GLXDrawable drawable, GLXContext ctx)
{
	Bool retval=0;  const char *renderer="Unknown";
	TRY();
	pbwin *pbw;  GLXFBConfig config=0;

	// Prevent recursion
	if(!_isremote(dpy)) return _glXMakeCurrent(dpy, drawable, ctx);
	////////////////////

		opentrace(glXMakeCurrent);  prargd(dpy);  prargx(drawable);  prargx(ctx);
		starttrace();

	// Find the FB config that was previously hashed to this context when it was
	// created.
	if(ctx) config=ctxh.findconfig(ctx);
	if(config==(GLXFBConfig)-1)
	{
		// Overlay context.  Hand off to the 2D X server.
		retval=_glXMakeCurrent(dpy, drawable, ctx);
		winh.setoverlay(dpy, drawable);
		stoptrace();  closetrace();
		return retval;
	}

	// glXMakeCurrent() implies a glFinish() on the previous context, which is
	// why we read back the front buffer here if it is dirty.
	GLXDrawable curdraw=_glXGetCurrentDrawable();
	if(glXGetCurrentContext() && _localdisplayiscurrent()
		&& curdraw && winh.findpb(curdraw, pbw))
	{
		pbwin *newpbw;
		if(drawable==0 || !winh.findpb(dpy, drawable, newpbw)
			|| newpbw->getglxdrawable()!=curdraw)
		{
			if(_drawingtofront() || pbw->_dirty)
				pbw->readback(GL_FRONT, false, false);
		}
	}

	// If the drawable isn't a window, we pass it through unmodified, else we
	// map it to a Pbuffer.
	if(dpy && drawable && ctx)
	{
		if(!config)
		{
			rrout.PRINTLN("[VGL] WARNING: glXMakeCurrent() called with a previously-destroyed context.");
			return False;
		}
		pbw=winh.setpb(dpy, drawable, config);
		if(pbw) drawable=pbw->updatedrawable();
		else if(!glxdh.getcurrentdpy(drawable))
		{
			// Apparently it isn't a Pbuffer or a Pixmap, so it must be a window
			// that was created in another application.  This code is necessary
			// to make CRUT (Chromium Utility Toolkit) applications work.
			if(_isremote(dpy))
			{
				winh.add(dpy, drawable);
				pbw=winh.setpb(dpy, drawable, config);
				if(pbw)
					drawable=pbw->updatedrawable();
			}
		}
	}

	retval=_glXMakeContextCurrent(_localdpy, drawable, drawable, ctx);
	if(fconfig.trace && retval) renderer=(const char *)glGetString(GL_RENDERER);
	// The pixels in a new Pbuffer are undefined, so we have to clear it.
	if(winh.findpb(drawable, pbw)) {pbw->clear();  pbw->cleanup();}
	pbpm *pbp;
	if((pbp=pmh.find(dpy, drawable))!=NULL) pbp->clear();
	// Needed to support color index rendering on Sun OpenGL
	#ifdef SUNOGL
	sunOglCurPrimTablePtr->oglIndexd=r_glIndexd;
	sunOglCurPrimTablePtr->oglIndexf=r_glIndexf;
	sunOglCurPrimTablePtr->oglIndexi=r_glIndexi;
	sunOglCurPrimTablePtr->oglIndexs=r_glIndexs;
	sunOglCurPrimTablePtr->oglIndexub=r_glIndexub;
	sunOglCurPrimTablePtr->oglIndexdv=r_glIndexdv;
	sunOglCurPrimTablePtr->oglIndexfv=r_glIndexfv;
	sunOglCurPrimTablePtr->oglIndexiv=r_glIndexiv;
	sunOglCurPrimTablePtr->oglIndexsv=r_glIndexsv;
	sunOglCurPrimTablePtr->oglIndexubv=r_glIndexubv;
	#endif

		stoptrace();  prargc(config);  prargx(drawable);  prargs(renderer);
		closetrace();

	CATCH();
	return retval;
}


Bool glXMakeContextCurrent(Display *dpy, GLXDrawable draw, GLXDrawable read,
	GLXContext ctx)
{
	Bool retval=0;  const char *renderer="Unknown";
	pbwin *pbw;  GLXFBConfig config=0;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXMakeContextCurrent(dpy, draw, read, ctx);
	////////////////////

		opentrace(glXMakeContextCurrent);  prargd(dpy);  prargx(draw);
		prargx(read);  prargx(ctx);  starttrace();

	if(ctx) config=ctxh.findconfig(ctx);
	if(config==(GLXFBConfig)-1)
	{
		// Overlay config.  Hand off to 2D X server.
		retval=_glXMakeContextCurrent(dpy, draw, read, ctx);
		winh.setoverlay(dpy, draw);
		winh.setoverlay(dpy, read);
		stoptrace();  closetrace();
		return retval;
	}

	// glXMakeContextCurrent() implies a glFinish() on the previous context,
	// which is why we read back the front buffer here if it is dirty.
	GLXDrawable curdraw=_glXGetCurrentDrawable();
	if(glXGetCurrentContext() && _localdisplayiscurrent()
	&& curdraw && winh.findpb(curdraw, pbw))
	{
		pbwin *newpbw;
		if(draw==0 || !winh.findpb(dpy, draw, newpbw)
			|| newpbw->getglxdrawable()!=curdraw)
		{
			if(_drawingtofront() || pbw->_dirty)
				pbw->readback(GL_FRONT, false, false);
		}
	}

	// If the drawable isn't a window, we pass it through unmodified, else we
	// map it to a Pbuffer.
	pbwin *drawpbw, *readpbw;
	if(dpy && (draw || read) && ctx)
	{
		if(!config)
		{
			rrout.PRINTLN("[VGL] WARNING: glXMakeContextCurrent() called with a previously-destroyed context");
			return False;
		}
		drawpbw=winh.setpb(dpy, draw, config);
		readpbw=winh.setpb(dpy, read, config);
		if(drawpbw)
			draw=drawpbw->updatedrawable();
		if(readpbw) read=readpbw->updatedrawable();
	}
	retval=_glXMakeContextCurrent(_localdpy, draw, read, ctx);
	if(fconfig.trace && retval) renderer=(const char *)glGetString(GL_RENDERER);
	if(winh.findpb(draw, drawpbw)) {drawpbw->clear();  drawpbw->cleanup();}
	if(winh.findpb(read, readpbw)) readpbw->cleanup();
	pbpm *pbp;
	if((pbp=pmh.find(dpy, draw))!=NULL) pbp->clear();
	#ifdef SUNOGL
	sunOglCurPrimTablePtr->oglIndexd=r_glIndexd;
	sunOglCurPrimTablePtr->oglIndexf=r_glIndexf;
	sunOglCurPrimTablePtr->oglIndexi=r_glIndexi;
	sunOglCurPrimTablePtr->oglIndexs=r_glIndexs;
	sunOglCurPrimTablePtr->oglIndexub=r_glIndexub;
	sunOglCurPrimTablePtr->oglIndexdv=r_glIndexdv;
	sunOglCurPrimTablePtr->oglIndexfv=r_glIndexfv;
	sunOglCurPrimTablePtr->oglIndexiv=r_glIndexiv;
	sunOglCurPrimTablePtr->oglIndexsv=r_glIndexsv;
	sunOglCurPrimTablePtr->oglIndexubv=r_glIndexubv;
	#endif

		stoptrace();  prargc(config);  prargx(draw);  prargx(read);
		prargs(renderer);  closetrace();

	CATCH();
	return retval;
}


Bool glXMakeCurrentReadSGI(Display *dpy, GLXDrawable draw, GLXDrawable read,
	GLXContext ctx)
{
	return glXMakeContextCurrent(dpy, draw, read, ctx);
}


// If 'ctx' was created for color index rendering, we need to fake the
// application into thinking that it's really a color index context, when in
// fact VGL is using an RGBA context behind the scenes.

int glXQueryContext(Display *dpy, GLXContext ctx, int attribute, int *value)
{
	int retval=0;
	if(ctxh.isoverlay(ctx)) return _glXQueryContext(dpy, ctx, attribute, value);

		opentrace(glXQueryContext);  prargd(dpy);  prargx(ctx);  prargi(attribute);
		starttrace();

	if(attribute==GLX_RENDER_TYPE)
	{
		int fbcid=-1;
		retval=_glXQueryContext(_localdpy, ctx, GLX_FBCONFIG_ID, &fbcid);
		if(fbcid>0)
		{
			VisualID vid=cfgh.getvisual(dpy, fbcid);
			if(vid && __vglVisualClass(dpy, DefaultScreen(dpy), vid)==PseudoColor
				&& value) *value=GLX_COLOR_INDEX_TYPE;
			else if(value) *value=GLX_RGBA_TYPE;
		}
	}
	else retval=_glXQueryContext(_localdpy, ctx, attribute, value);

		stoptrace();  if(value) prargi(*value);  closetrace();

	return retval;
}


// Hand off to the 2D X server (overlay rendering) or the 3D X server (opaque
// rendering) without modification

int glXQueryContextInfoEXT(Display *dpy, GLXContext ctx, int attribute,
	int *value)
{
	if(ctxh.isoverlay(ctx))
		return _glXQueryContextInfoEXT(dpy, ctx, attribute, value);

	return _glXQueryContextInfoEXT(_localdpy, ctx, attribute, value);
}


// Hand off to the 2D X server (overlay rendering) or the 3D X server (opaque
// rendering) without modification, except that, if 'draw' is not an overlay
// window, we replace it with its corresponding Pbuffer ID.

void glXQueryDrawable(Display *dpy, GLXDrawable draw, int attribute,
	unsigned int *value)
{
	TRY();

		opentrace(glXQueryDrawable);  prargd(dpy);  prargx(draw);  prargi(attribute);
		starttrace();

	if(winh.isoverlay(dpy, draw))
	{
		_glXQueryDrawable(dpy, draw, attribute, value);
		stoptrace(); if(value) {prargi(*value);}  closetrace();
		return;
	}

	_glXQueryDrawable(_localdpy, ServerDrawable(dpy, draw), attribute, value);

		stoptrace();  prargx(ServerDrawable(dpy, draw));  if(value) {prargi(*value);}
		closetrace();

	CATCH();
}

#ifdef SUNOGL
void glXQueryGLXPbufferSGIX(Display *dpy, GLXPbuffer pbuf, int attribute,
	unsigned int *value)
#else
int glXQueryGLXPbufferSGIX(Display *dpy, GLXPbuffer pbuf, int attribute,
	unsigned int *value)
#endif
{
	glXQueryDrawable(dpy, pbuf, attribute, value);
	#ifndef SUNOGL
	return 0;
	#endif
}


// Hand off to the 3D X server without modification.

Bool glXQueryExtension(Display *dpy, int *error_base, int *event_base)
{
	return _glXQueryExtension(_localdpy, error_base, event_base);
}


// Same as glXGetClientString(GLX_EXTENSIONS)

const char *glXQueryExtensionsString(Display *dpy, int screen)
{
	// If this is called internally to OpenGL, use the real function.
	if(!_isremote(dpy)) return _glXQueryExtensionsString(dpy, screen);
	////////////////////
	return glxextensions;
}


// Same as glXGetClientString() in our case

const char *glXQueryServerString(Display *dpy, int screen, int name)
{
	// If this is called internally to OpenGL, use the real function.
	if(!_isremote(dpy)) return _glXQueryServerString(dpy, screen, name);
	////////////////////
	if(name==GLX_EXTENSIONS) return glxextensions;
	else if(name==GLX_VERSION) return "1.4";
	else if(name==GLX_VENDOR) return __APPNAME;
	else return NULL;
}


// Hand off to the 3D X server without modification.

Bool glXQueryVersion(Display *dpy, int *major, int *minor)
{
	return _glXQueryVersion(_localdpy, major, minor);
}


// Hand off to the 2D X server (overlay rendering) or the 3D X server (opaque
// rendering) without modification.

void glXSelectEvent(Display *dpy, GLXDrawable draw, unsigned long event_mask)
{
	if(winh.isoverlay(dpy, draw)) return _glXSelectEvent(dpy, draw, event_mask);

	_glXSelectEvent(_localdpy, ServerDrawable(dpy, draw), event_mask);
}

void glXSelectEventSGIX(Display *dpy, GLXDrawable drawable, unsigned long mask)
{
	glXSelectEvent(dpy, drawable, mask);
}


// If the application is rendering to the back buffer, VirtualGL will read
// back and send the buffer whenever glXSwapBuffers() is called.

void glXSwapBuffers(Display* dpy, GLXDrawable drawable)
{
	TRY();

		opentrace(glXSwapBuffers);  prargd(dpy);  prargx(drawable);  starttrace();

	if(winh.isoverlay(dpy, drawable))
	{
		_glXSwapBuffers(dpy, drawable);
		stoptrace();  closetrace();  
		return;
	}

	fconfig.flushdelay=0.;
	pbwin *pbw=NULL;
	if(_isremote(dpy) && winh.findpb(dpy, drawable, pbw))
	{
		pbw->readback(GL_BACK, false, fconfig.sync);
		pbw->swapbuffers();
	}
	else _glXSwapBuffers(_localdpy, drawable);

		stoptrace();  if(_isremote(dpy) && pbw) {prargx(pbw->getglxdrawable());}
		closetrace();  

	CATCH();
}


// Returns the transparent index from the overlay visual on the 2D X server

#ifdef SUNOGL
GLboolean glXGetTransparentIndexSUN(Display *dpy, Window overlay,
	Window underlay, unsigned int *transparentIndex)
#else
int glXGetTransparentIndexSUN(Display *dpy, Window overlay,
	Window underlay, long *transparentIndex)
#endif
{
	XWindowAttributes xwa;
	if(!transparentIndex) return False;

		opentrace(glXGetTransparentIndexSUN);  prargd(dpy);  prargx(overlay);
		prargx(underlay);  starttrace();

	if(fconfig.transpixel>=0)
		*transparentIndex=fconfig.transpixel;
	else
	{
		if(!dpy || !overlay) return False;
		XGetWindowAttributes(dpy, overlay, &xwa);
		*transparentIndex=__vglClientVisualAttrib(dpy, DefaultScreen(dpy),
			xwa.visual->visualid, GLX_TRANSPARENT_INDEX_VALUE);
	}

		stoptrace();  prargi(*transparentIndex);  closetrace();

	return True;
}


// Hand off to the 3D X server without modification, except that 'drawable' is
// replaced with its corresponding Pbuffer ID.

Bool glXJoinSwapGroupNV(Display *dpy, GLXDrawable drawable, GLuint group)
{
	return _glXJoinSwapGroupNV(_localdpy, ServerDrawable(dpy, drawable), group);
}


// Hand off to the 3D X server without modification.

Bool glXBindSwapBarrierNV(Display *dpy, GLuint group, GLuint barrier)
{
	return _glXBindSwapBarrierNV(_localdpy, group, barrier);
}


// Hand off to the 3D X server without modification, except that 'drawable' is
// replaced with its corresponding Pbuffer ID.

Bool glXQuerySwapGroupNV(Display *dpy, GLXDrawable drawable, GLuint *group,
	GLuint *barrier)
{
	return _glXQuerySwapGroupNV(_localdpy, ServerDrawable(dpy, drawable), group,
		barrier);
}


// Hand off to the 3D X server without modification.

Bool glXQueryMaxSwapGroupsNV(Display *dpy, int screen, GLuint *maxGroups,
	GLuint *maxBarriers)
{
	return _glXQueryMaxSwapGroupsNV(_localdpy, DefaultScreen(_localdpy),
		maxGroups, maxBarriers);
}


// Hand off to the 3D X server without modification.

Bool glXQueryFrameCountNV(Display *dpy, int screen, GLuint *count)
{
	return _glXQueryFrameCountNV(_localdpy, DefaultScreen(_localdpy), count);
}


// Hand off to the 3D X server without modification.

Bool glXResetFrameCountNV(Display *dpy, int screen)
{
	return _glXResetFrameCountNV(_localdpy, DefaultScreen(_localdpy));
}


// Recent releases of the nVidia drivers always try to send this function to
// the 2D X server for some reason, and some apps don't bother to ask VirtualGL
// whether it supports GLX_SGI_swap_control, so we have to interpose this
// function out of existence (it isn't relevant with Pbuffers, anyhow.)

int glXSwapIntervalSGI(int interval)
{
	if(fconfig.trace)
		rrout.print("[VGL] glXSwapIntervalSGI() [NOT SUPPORTED]\n");
	return 0;
}


#include "xfonts.c"

// We use a tweaked out version of the Mesa glXUseXFont() implementation.

void glXUseXFont(Font font, int first, int count, int list_base)
{
	TRY();

		opentrace(glXUseXFont);  prargx(font);  prargi(first);  prargi(count);
		prargi(list_base);  starttrace();

	if(ctxh.overlaycurrent()) _glXUseXFont(font, first, count, list_base);
	else Fake_glXUseXFont(font, first, count, list_base);

		stoptrace();  closetrace();

	return;
	CATCH();
}


} // extern "C"