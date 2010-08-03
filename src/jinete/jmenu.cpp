/* Jinete - a GUI library
 * Copyright (C) 2003-2010 David Capello.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of the author nor the names of its contributors may
 *     be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <allegro.h>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>

#include "jinete/jinete.h"
#include "jinete/jintern.h"
#include "Vaca/Size.h"

/**********************************************************************/
/* Internal messages: to move between menus */

JM_MESSAGE(open_menuitem);
JM_MESSAGE(close_menuitem);
JM_MESSAGE(close_popup);
JM_MESSAGE(exe_menuitem);

/**
 * bool select_first = msg->user.a;
 */
#define JM_OPEN_MENUITEM  jm_open_menuitem()

/**
 * bool last_of_close_chain = msg->user.a;
 */
#define JM_CLOSE_MENUITEM jm_close_menuitem()

#define JM_CLOSE_POPUP    jm_close_popup()

#define JM_EXE_MENUITEM   jm_exe_menuitem()

/**********************************************************************/
/* Some auxiliar matros */

#define MOUSE_IN(pos)						\
  ((jmouse_x(0) >= pos->x1) && (jmouse_x(0) < pos->x2) &&	\
   (jmouse_y(0) >= pos->y1) && (jmouse_y(0) < pos->y2))

#define MBOX(widget)						\
  ((MenuBox *)jwidget_get_data(((JWidget)widget), JI_MENUBOX))

#define MENU(widget)						\
  ((Menu *)jwidget_get_data(((JWidget)widget), JI_MENU))

#define MITEM(widget)						\
  ((MenuItem *)jwidget_get_data(((JWidget)widget), JI_MENUITEM))

#define HAS_SUBMENU(menuitem)					\
  ((MITEM(menuitem)->submenu) &&				\
   (!jlist_empty(MITEM(menuitem)->submenu->children)))

/* data for the main jmenubar or the first popuped-jmenubox */
typedef struct Base
{
  /* true when the menu-items must be opened with the cursor
     movement */
  bool was_clicked : 1;

  /* true when there's JM_OPEN/CLOSE_MENUITEM messages in queue, to
     avoid start processing another menuitem-request when we're
     already working in one */
  bool is_processing : 1;

  /* true when the JM_BUTTONPRESSED is being filtered */
  bool is_filtering : 1;

  bool close_all : 1;
} Base;

/* data for a jmenu */
typedef struct Menu
{
  JWidget menuitem;		/* from where the menu was open */
} Menu;

/* data for a jmenubox */
typedef struct MenuBox
{
  Base *base;
} MenuBox;

/* data for a jmenuitem */
typedef struct MenuItem
{
  JAccel accel;			/* hot-key */
  bool highlight : 1;		/* is highlighted? */
  JWidget submenu;		/* the sub-menu */
  JWidget submenu_menubox;	/* the opened menubox for this menu-item */
} MenuItem;

static bool menu_msg_proc(JWidget widget, JMessage msg);
static void menu_request_size(JWidget widget, int *w, int *h);
static void menu_set_position(JWidget widget, JRect rect);

static bool menubox_msg_proc(JWidget widget, JMessage msg);
static void menubox_request_size(JWidget widget, int *w, int *h);
static void menubox_set_position(JWidget widget, JRect rect);

static bool menuitem_msg_proc(JWidget widget, JMessage msg);
static void menuitem_request_size(JWidget widget, int *w, int *h);

static JWidget get_base_menubox(JWidget widget);
static Base *get_base(JWidget widget);
static Base *create_base(JWidget widget);

static JWidget get_highlight(JWidget menu);
static void set_highlight(JWidget menu, JWidget menuitem, bool click, bool open_submenu, bool select_first_child);
static void unhighlight(JWidget menu);

static void open_menuitem(JWidget menuitem, bool select_first);
static void close_menuitem(JWidget menuitem, bool last_of_close_chain);
static void close_popup(JWidget menubox);
static void close_all(JWidget menu);
static void exe_menuitem(JWidget menuitem);
static bool window_msg_proc(JWidget widget, JMessage msg);

static JWidget check_for_letter(JWidget menu, int ascii);
static JWidget check_for_accel(JWidget menu, JMessage msg);

static JWidget find_nextitem(JWidget menu, JWidget menuitem);
static JWidget find_previtem(JWidget menu, JWidget menuitem);

JWidget jmenu_new()
{
  JWidget widget = new Widget(JI_MENU);
  Menu *menu = jnew(Menu, 1);

  menu->menuitem = NULL;

  jwidget_add_hook(widget, JI_MENU, menu_msg_proc, menu);
  jwidget_init_theme(widget);

  return widget;
}

JWidget jmenubar_new()
{
  JWidget widget = jmenubox_new();

  create_base(widget);

  widget->type = JI_MENUBAR;
  jwidget_init_theme(widget);

  return widget;
}

JWidget jmenubox_new()
{
  JWidget widget = new Widget(JI_MENUBOX);
  MenuBox *menubox = jnew(MenuBox, 1);

  menubox->base = NULL;

  jwidget_add_hook(widget, JI_MENUBOX, menubox_msg_proc, menubox);
  jwidget_focusrest(widget, true);
  jwidget_init_theme(widget);

  return widget;
}

JWidget jmenuitem_new(const char *text)
{
  JWidget widget = new Widget(JI_MENUITEM);
  MenuItem *menuitem = jnew(MenuItem, 1);

  menuitem->accel = NULL;
  menuitem->highlight = false;
  menuitem->submenu = NULL;
  menuitem->submenu_menubox = NULL;

  jwidget_add_hook(widget, JI_MENUITEM, menuitem_msg_proc, menuitem);
  widget->setText(text);
  jwidget_init_theme(widget);

  return widget;
}

JWidget jmenubox_get_menu(JWidget widget)
{
  assert_valid_widget(widget);

  if (jlist_empty(widget->children))
    return NULL;
  else
    return (JWidget)jlist_first(widget->children)->data;
}

JWidget jmenubar_get_menu(JWidget widget)
{
  assert_valid_widget(widget);

  return jmenubox_get_menu(widget);
}

JWidget jmenuitem_get_submenu(JWidget widget)
{
  MenuItem *menuitem;

  assert_valid_widget(widget);

  menuitem = MITEM(widget);

  return menuitem->submenu ? menuitem->submenu: NULL;
}

JAccel jmenuitem_get_accel(JWidget widget)
{
  assert_valid_widget(widget);

  return MITEM(widget)->accel;
}

bool jmenuitem_has_submenu_opened(JWidget widget)
{
  assert_valid_widget(widget);

  return MITEM(widget)->submenu_menubox != NULL;
}

void jmenubox_set_menu(JWidget widget, JWidget widget_menu)
{
  JWidget old_menu;

  assert_valid_widget(widget);

  old_menu = jmenubox_get_menu(widget);
  if (old_menu)
    jwidget_remove_child(widget, old_menu);

  if (widget_menu) {
    assert_valid_widget(widget_menu);
    jwidget_add_child(widget, widget_menu);
  }
}

void jmenubar_set_menu(JWidget widget, JWidget widget_menu)
{
  assert_valid_widget(widget);

  jmenubox_set_menu(widget, widget_menu);
}

void jmenuitem_set_submenu(JWidget widget, JWidget widget_menu)
{
  MenuItem *menuitem;

  assert_valid_widget(widget);

  menuitem = MITEM(widget);

  if (menuitem->submenu)
    MENU(menuitem->submenu)->menuitem = NULL;

  menuitem->submenu = widget_menu;

  if (menuitem->submenu) {
    assert_valid_widget(widget_menu);
    MENU(menuitem->submenu)->menuitem = widget; 
  }
}

/**
 * Changes the keyboard shortcuts (accelerators) for the specified
 * widget (a menu-item).
 *
 * @warning The specified @a accel will be freed automatically when
 *          the menu-item'll receive JM_DESTROY message.
 */
void jmenuitem_set_accel(JWidget widget, JAccel accel)
{
  MenuItem *menuitem;

  assert_valid_widget(widget);

  menuitem = MITEM(widget);
  if (menuitem->accel)
    jaccel_free(menuitem->accel);

  menuitem->accel = accel;
}

int jmenuitem_is_highlight(JWidget widget)
{
  assert_valid_widget(widget);

  return MITEM(widget)->highlight;
}

void jmenu_popup(JWidget menu, int x, int y)
{
  Base *base;

  do {
    jmouse_poll();
  } while (jmouse_b(0));

  // new window and new menu-box
  Frame* window = new Frame(false, NULL);
  Widget* menubox = jmenubox_new();

  base = create_base(menubox);
  base->was_clicked = true;
  base->is_filtering = true;
  jmanager_add_msg_filter(JM_BUTTONPRESSED, menubox);

  window->set_moveable(false);	 /* can't move the window */

  /* set children */
  jmenubox_set_menu(menubox, menu);
  jwidget_add_child(window, menubox);

  window->remap_window();

  /* menubox position */
  window->position_window(MID(0, x, JI_SCREEN_W-jrect_w(window->rc)),
			  MID(0, y, JI_SCREEN_H-jrect_h(window->rc)));

  /* set the focus to the new menubox */
  jmanager_set_focus(menubox);
  jwidget_magnetic(menubox, true);

  /* open the window */
  window->open_window_fg();

  /* free the keyboard focus */
  jmanager_free_focus();

  /* fetch the "menu" so it isn't destroyed */
  jmenubox_set_menu(menubox, NULL);

  /* destroy the window */
  jwidget_free(window);
}

static bool menu_msg_proc(JWidget widget, JMessage msg)
{
  switch (msg->type) {

    case JM_DESTROY: {
      Menu *menu = MENU(widget);
      assert(menu != NULL);

      if (menu->menuitem) {
	if (MITEM(menu->menuitem)->submenu == widget) {
	  MITEM(menu->menuitem)->submenu = NULL;
	}
	else {
	  assert(MITEM(menu->menuitem)->submenu == NULL);
	}
      }

      jfree(menu);
      break;
    }

    case JM_REQSIZE:
      menu_request_size(widget, &msg->reqsize.w, &msg->reqsize.h);
      return true;

    case JM_SETPOS:
      menu_set_position(widget, &msg->setpos.rect);
      return true;

    case JM_DRAW:
      widget->theme->draw_menu(widget, &msg->draw.rect);
      return true;

  }

  return false;
}

static void menu_request_size(JWidget widget, int *w, int *h)
{
  Size reqSize;
  JLink link;

  *w = *h = 0;

  JI_LIST_FOR_EACH(widget->children, link) {
    reqSize = ((Widget*)link->data)->getPreferredSize();

    if (widget->parent->type == JI_MENUBAR) {
      *w += reqSize.w + ((link->next != widget->children->end) ?
			 widget->child_spacing: 0);
      *h = MAX(*h, reqSize.h);
    }
    else {
      *w = MAX(*w, reqSize.w);
      *h += reqSize.h + ((link->next != widget->children->end) ?
			 widget->child_spacing: 0);
    }
  }

  *w += widget->border_width.l + widget->border_width.r;
  *h += widget->border_width.t + widget->border_width.b;
}

static void menu_set_position(JWidget widget, JRect rect)
{
  Size reqSize;
  JWidget child;
  JRect cpos;
  JLink link;

  jrect_copy(widget->rc, rect);
  cpos = jwidget_get_child_rect(widget);

  JI_LIST_FOR_EACH(widget->children, link) {
    child = (JWidget)link->data;

    reqSize = child->getPreferredSize();

    if (widget->parent->type == JI_MENUBAR)
      cpos->x2 = cpos->x1+reqSize.w;
    else
      cpos->y2 = cpos->y1+reqSize.h;

    jwidget_set_rect(child, cpos);

    if (widget->parent->type == JI_MENUBAR)
      cpos->x1 += jrect_w(cpos);
    else
      cpos->y1 += jrect_h(cpos);
  }

  jrect_free(cpos);
}

static bool menubox_msg_proc(JWidget widget, JMessage msg)
{
  JWidget menu = jmenubox_get_menu(widget);

  switch (msg->type) {

    case JM_DESTROY: {
      MenuBox* menubox = reinterpret_cast<MenuBox*>(jwidget_get_data(widget, JI_MENUBOX));
      assert(menubox != NULL);

      if (menubox->base != NULL &&
	  menubox->base->is_filtering) {
	menubox->base->is_filtering = false;
	jmanager_remove_msg_filter(JM_BUTTONPRESSED, widget);
      }

      if (menubox->base)
	jfree(menubox->base);

      jfree(menubox);
      break;
    }

    case JM_REQSIZE:
      menubox_request_size(widget, &msg->reqsize.w, &msg->reqsize.h);
      return true;

    case JM_SETPOS:
      menubox_set_position(widget, &msg->setpos.rect);
      return true;

    case JM_MOTION:
      /* isn't pressing a button? */
      if (!msg->mouse.flags && !get_base(widget)->was_clicked)
	break;

      /* fall though */

    case JM_BUTTONPRESSED:
      if (menu) {
	JWidget picked;

	if (get_base(widget)->is_processing)
	  break;
	
	/* here we catch the filtered messages (menu-bar or the popuped
	   menu-box) to detect if the user press outside of the widget */
	if (msg->type == JM_BUTTONPRESSED
	    && MBOX(widget)->base != NULL) {
	  JWidget picked = jwidget_pick(ji_get_default_manager(),
					msg->mouse.x, msg->mouse.y);

	  /* if one of these conditions are accomplished we have to
	     close all menus (back to menu-bar or close the popuped menubox),
	     this is the place where we control if */
	  if (picked == NULL ||  /* if the button was clicked nowhere */
	      picked == widget ||	/* if the button was clicked in this menubox */
	      /* the picked widget isn't menu-related */
	      (picked->type != JI_MENUBOX &&
	       picked->type != JI_MENUBAR &&
	       picked->type != JI_MENUITEM) ||
	      /* the picked widget (that is menu-related) isn't from the
		 same tree of menus */
	      (get_base_menubox(picked) != widget)) {

	    /* the user click outside all the menu-box/menu-items, close all */
	    close_all(menu);
	    return true;
	  }
	}

	/* get the widget below the mouse cursor  */
	picked = jwidget_pick(menu, msg->mouse.x, msg->mouse.y);
	if (picked) {
	  if ((picked->type == JI_MENUITEM) &&
	      !(picked->flags & JI_DISABLED) &&
	      !MITEM(picked)->highlight) {
	    set_highlight(menu, picked, false, true, false);
	  }
	  else if (!get_base(widget)->was_clicked) {
	    unhighlight(menu);
	  }
	}
      }
      break;

    case JM_MOUSELEAVE:
      if (menu) {
	JWidget highlight;

	if (get_base(widget)->is_processing)
	  break;
	
	highlight = get_highlight(menu);
	if ((highlight) && (!MITEM(highlight)->submenu_menubox))
	  unhighlight(menu);
      }
      break;

    case JM_BUTTONRELEASED:
      if (menu) {
	JWidget highlight = get_highlight(menu);

	if (get_base(widget)->is_processing)
	  break;

	/* the item is highlighted and not opened */
	if ((highlight) && (!MITEM(highlight)->submenu_menubox)) {
	  close_all(menu);
	  exe_menuitem(highlight);
	}
      }
      break;

    case JM_KEYPRESSED:
      if (menu) {
	JWidget selected;

	if (get_base(widget)->is_processing)
	  break;

	get_base(widget)->was_clicked = false;

	/* check for ALT+some letter in menubar and some letter in menuboxes */
	if (((widget->type == JI_MENUBOX) && (!msg->any.shifts)) ||
	    ((widget->type == JI_MENUBAR) && (msg->any.shifts & KB_ALT_FLAG))) {
	  selected = check_for_letter(menu, scancode_to_ascii(msg->key.scancode));
	  if (selected) {
	    set_highlight(menu, selected, true, true, true);
	    return true;
	  }
	}

#if 0				/* TODO check this for jinete-tests */
	/* only in menu-bars... */
	if (widget->type == JI_MENUBAR) {
	  /* ...check for accelerators */
	  selected = check_for_accel(menubox->menu, msg);
	  if (selected) {
	    close_all(menu);
	    exe_menuitem(selected);
	    return true;
	  }
	}
#endif

	/* highlight movement with keyboard */
	if (widget->hasFocus()) {
	  JWidget highlight = get_highlight(menu);
	  JWidget child;
	  JWidget child_with_submenu_opened = NULL;
	  JLink link;
	  bool used = false;

	  /* search a child with highlight or the submenu opened */
	  JI_LIST_FOR_EACH(menu->children, link) {
	    child = (JWidget)link->data;

	    if (child->type != JI_MENUITEM)
	      continue;

	    if (MITEM(child)->submenu_menubox)
	      child_with_submenu_opened = child;
	  }

	  if (!highlight && child_with_submenu_opened)
	    highlight = child_with_submenu_opened;
	  
	  switch (msg->key.scancode) {

	    case KEY_ESC:
	      /* in menu-bar */
	      if (widget->type == JI_MENUBAR) {
		if (highlight) {
		  close_all(menu);

		  /* fetch the focus */
		  jmanager_free_focus();
		  used = true;
		}
	      }
	      /* in menu-boxes */
	      else {
		if (child_with_submenu_opened) {
		  close_menuitem(child_with_submenu_opened, true);
		  used = true;
		}
		/* go to parent */
		else if (MENU(menu)->menuitem) {
		  /* just retrogress one parent-level */
		  close_menuitem(MENU(menu)->menuitem, true);
		  used = true;
		}
	      }
	      break;

	    case KEY_UP:
              /* in menu-bar */
	      if (widget->type == JI_MENUBAR) {
		if (child_with_submenu_opened)
		  close_menuitem(child_with_submenu_opened, true);
              }
              /* in menu-boxes */
              else {
                /* go to previous */
                highlight = find_previtem(menu, highlight);
		set_highlight(menu, highlight, false, false, false);
              }
	      used = true;
	      break;

            case KEY_DOWN:
              /* in menu-bar */
	      if (widget->type == JI_MENUBAR) {
                /* select the active menu */
		set_highlight(menu, highlight, true, true, true);
              }
              /* in menu-boxes */
              else {
                /* go to next */
                highlight = find_nextitem(menu, highlight);
		set_highlight(menu, highlight, false, false, false);
              }
	      used = true;
	      break;

	    case KEY_LEFT:
              /* in menu-bar */
	      if (widget->type == JI_MENUBAR) {
                /* go to previous */
                highlight = find_previtem(menu, highlight);
		set_highlight(menu, highlight, false, false, false);
              }
              /* in menu-boxes */
	      else {
                /* go to parent */
                if (MENU(menu)->menuitem) {
                  JWidget menuitem;
                  JWidget parent = MENU(menu)->menuitem->parent->parent;

                  /* go to the previous item in the parent */

                  /* if the parent is the menu-bar */
                  if (parent->type == JI_MENUBAR) {
		    menu = jmenubox_get_menu(parent);
                    menuitem = find_previtem(menu, get_highlight(menu));

                    /* go to previous item in the parent */
		    set_highlight(menu, menuitem, false, true, true);
                  }
                  /* if the parent isn't the menu-bar */
                  else {
                    /* just retrogress one parent-level */
		    close_menuitem(MENU(menu)->menuitem, true);
		  }
                }
	      }
	      used = true;
	      break;

	    case KEY_RIGHT:
              /* in menu-bar */
	      if (widget->type == JI_MENUBAR) {
                /* go to next */
                highlight = find_nextitem(menu, highlight);
		set_highlight(menu, highlight, false, false, false);
              }
              /* in menu-boxes */
              else {
                /* enter in sub-menu */
                if ((highlight) && HAS_SUBMENU(highlight)) {
		  set_highlight(menu, highlight, true, true, true);
                }
                /* go to parent */
                else if (MENU(menu)->menuitem) {
                  JWidget root, menuitem;

                  /* get the root menu */
		  root = get_base_menubox(widget);
		  menu = jmenubox_get_menu(root);

                  /* go to the next item in the root */
                  menuitem = find_nextitem(menu, get_highlight(menu));

                  /* open the sub-menu */
		  set_highlight(menu, menuitem, false, true, true);
                }
              }
	      used = true;
	      break;

	    case KEY_ENTER:
	    case KEY_ENTER_PAD:
	      if (highlight)
		set_highlight(menu, highlight, true, true, true);
	      used = true;
	      break;
	  }

	  return used;
	}
      }
      break;

    default:
      if (msg->type == JM_CLOSE_POPUP) {
	_jmanager_close_window(widget->getManager(),
			       static_cast<Frame*>(widget->getRoot()), true);
      }
      break;

  }

  return false;
}

static void menubox_request_size(JWidget widget, int *w, int *h)
{
  JWidget menu = jmenubox_get_menu(widget);

  if (menu) {
    Size reqSize = menu->getPreferredSize();
    *w = reqSize.w;
    *h = reqSize.h;
  }
  else
    *w = *h = 0;

  *w += widget->border_width.l + widget->border_width.r;
  *h += widget->border_width.t + widget->border_width.b;
}

static void menubox_set_position(JWidget widget, JRect rect)
{
  JWidget menu = jmenubox_get_menu(widget);

  jrect_copy(widget->rc, rect);

  if (menu) {
    JRect cpos = jwidget_get_child_rect(widget);
    jwidget_set_rect(menu, cpos);
    jrect_free(cpos);
  }
}

static bool menuitem_msg_proc(JWidget widget, JMessage msg)
{
  MenuItem *menuitem = MITEM(widget);

  switch (msg->type) {

    case JM_DESTROY:
      assert(menuitem != NULL);

      if (menuitem->accel)
	jaccel_free(menuitem->accel);

      if (menuitem->submenu)
	jwidget_free(menuitem->submenu);

      jfree(menuitem);
      break;

    case JM_REQSIZE:
      menuitem_request_size(widget, &msg->reqsize.w, &msg->reqsize.h);
      return true;

    case JM_DRAW:
      widget->theme->draw_menuitem(widget, &msg->draw.rect);
      return true;

    case JM_MOUSEENTER:
    case JM_MOUSELEAVE:
      /* TODO theme specific!! */
      jwidget_dirty(widget);
      break;

    default:
      if (msg->type == JM_OPEN_MENUITEM) {
	Base* base = get_base(widget);
	Frame* window;
	Widget* menubox;
	JRect pos, old_pos;
	bool select_first = msg->user.a ? true: false;

	assert(base != NULL);
	assert(base->is_processing);
	assert(HAS_SUBMENU(widget));

	old_pos = jwidget_get_rect(widget->parent->parent);

	/* new window and new menu-box */
	window = new Frame(false, NULL);
	jwidget_add_hook(window, -1, window_msg_proc, NULL);

	menubox = jmenubox_new();

	menuitem->submenu_menubox = menubox;

	window->set_moveable(false); // can't move the window

	/* set children */
	jmenubox_set_menu(menubox, menuitem->submenu);
	jwidget_add_child(window, menubox);

	window->remap_window();

	/* menubox position */
	pos = jwidget_get_rect(window);

	if (widget->parent->parent->type == JI_MENUBAR) {
	  jrect_moveto(pos,
		       MID(0, widget->rc->x1, JI_SCREEN_W-jrect_w(pos)),
		       MID(0, widget->rc->y2, JI_SCREEN_H-jrect_h(pos)));
	}
	else {
	  int x_left = widget->rc->x1-jrect_w(pos);
	  int x_right = widget->rc->x2;
	  int x, y = widget->rc->y1;
	  struct jrect r1, r2;
	  int s1, s2;

	  r1.x1 = x_left = MID(0, x_left, JI_SCREEN_W-jrect_w(pos));
	  r2.x1 = x_right = MID(0, x_right, JI_SCREEN_W-jrect_w(pos));

	  r1.y1 = r2.y1 = y = MID(0, y, JI_SCREEN_H-jrect_h(pos));

	  r1.x2 = r1.x1+jrect_w(pos);
	  r1.y2 = r1.y1+jrect_h(pos);
	  r2.x2 = r2.x1+jrect_w(pos);
	  r2.y2 = r2.y1+jrect_h(pos);

	  /* calculate both intersections */
	  s1 = jrect_intersect(&r1, old_pos);
	  s2 = jrect_intersect(&r2, old_pos);

	  if (!s2)
	    x = x_right;		/* use the right because there aren't
					   intersection with it */
	  else if (!s1)
	    x = x_left;		/* use the left because there are not
				   intersection */
	  else if (jrect_w(&r2)*jrect_h(&r2) <= jrect_w(&r1)*jrect_h(&r1))
	    x = x_right;	        /* use the right because there are less
					   intersection area */
	  else
	    x = x_left;		/* use the left because there are less
				   intersection area */

	  jrect_moveto(pos, x, y);
	}

	window->position_window(pos->x1, pos->y1);
	jrect_free(pos);

	/* set the focus to the new menubox */
	jwidget_magnetic(menubox, true);

	/* setup the highlight of the new menubox */
	if (select_first) {
	  /* select the first child */
	  JWidget child, first_child = NULL;
	  JLink link;

	  JI_LIST_FOR_EACH(menuitem->submenu->children, link) {
	    child = (JWidget)link->data;

	    if (child->type != JI_MENUITEM)
	      continue;

	    if (child->isEnabled()) {
	      first_child = child;
	      break;
	    }
	  }

	  if (first_child)
	    set_highlight(menuitem->submenu, first_child, false, false, false);
	  else
	    unhighlight(menuitem->submenu);
	}
	else
	  unhighlight(menuitem->submenu);

	/* run in background */
	window->open_window_bg();

	base->is_processing = false;

	jrect_free(old_pos);
	return true;
      }
      else if (msg->type == JM_CLOSE_MENUITEM) {
	Base* base = get_base(widget);
	Frame* window;
	Widget* menubox;
	bool last_of_close_chain = msg->user.a ? true: false;

	assert(base != NULL);
	assert(base->is_processing);

	menubox = menuitem->submenu_menubox;
	menuitem->submenu_menubox = NULL;

	assert(menubox != NULL);

	window = (Frame*)menubox->parent;
	assert(window && window->type == JI_FRAME);

	/* fetch the "menu" to avoid free it with 'jwidget_free()' */
	jmenubox_set_menu(menubox, NULL);

	/* destroy the window */
	window->closeWindow(NULL);

	/* set the focus to this menu-box of this menu-item */
	if (base->close_all)
	  jmanager_free_focus();
	else
	  jmanager_set_focus(widget->parent->parent);

	/* is not necessary to free this window because it's
	   automatically destroyed by the manager
	   ... jwidget_free(window);
	*/

	if (last_of_close_chain) {
	  base->close_all = false;
	  base->is_processing = false;
	}

	return true;
      }
      else if (msg->type == JM_EXE_MENUITEM) {
	jwidget_emit_signal(widget, JI_SIGNAL_MENUITEM_SELECT);
	return true;
      }
      break;

  }

  return false;
}

static void menuitem_request_size(JWidget widget, int *w, int *h)
{
  MenuItem *menuitem = MITEM(widget);
  int bar = widget->parent->parent->type == JI_MENUBAR;

  if (widget->hasText()) {
    *w =
      + widget->border_width.l
      + jwidget_get_text_length(widget)
      + ((bar) ? widget->child_spacing/4: widget->child_spacing)
      + widget->border_width.r;

    *h =
      + widget->border_width.t
      + jwidget_get_text_height(widget)
      + widget->border_width.b;

    if (menuitem->accel) {
      char buf[256];
      jaccel_to_string(menuitem->accel, buf);
      *w += ji_font_text_len(widget->getFont(), buf);
    }
  }
  else {
    *w = *h = 0;
  }
}

/**
 * Clims the hierarchy of menus to get the most-top menubox.
 */
static JWidget get_base_menubox(JWidget widget)
{
  while (widget != NULL) {
    assert_valid_widget(widget);

    /* we are in a menubox */
    if (widget->type == JI_MENUBOX || widget->type == JI_MENUBAR) {
      if (MBOX(widget)->base != NULL) {
	return widget;
      }
      else {
	JWidget menu = jmenubox_get_menu(widget);
	
	assert(menu != NULL);
	assert(MENU(menu)->menuitem != NULL);

	widget = MENU(menu)->menuitem;
      }
    }
    /* we are in a menuitem */
    else {
      assert(widget->type == JI_MENUITEM);
      assert(widget->parent != NULL);
      assert(widget->parent->type == JI_MENU);

      widget = widget->parent->parent;
    }
  }

  assert(false);
  return NULL;
}

static Base *get_base(JWidget widget)
{
  widget = get_base_menubox(widget);
  return MBOX(widget)->base;
}

static Base *create_base(JWidget widget)
{
  Base *base = jnew(Base, 1);

  base->was_clicked = false;
  base->is_filtering = false;
  base->is_processing = false;
  base->close_all = false;

  MBOX(widget)->base = base;

  return base;
}

static JWidget get_highlight(JWidget menu)
{
  JWidget child;
  JLink link;

  JI_LIST_FOR_EACH(menu->children, link) {
    child = (JWidget)link->data;

    if (child->type != JI_MENUITEM)
      continue;

    if (MITEM(child)->highlight)
      return child;
  }

  return NULL;
}

static void set_highlight(JWidget menu, JWidget menuitem, bool click, bool open_submenu, bool select_first_child)
{
  JWidget child;
  JLink link;

  /* find the menuitem with the highlight */
  JI_LIST_FOR_EACH(menu->children, link) {
    child = (JWidget)link->data;

    if (child->type != JI_MENUITEM)
      continue;

    if (child != menuitem) {
      /* is it? */
      if (MITEM(child)->highlight) {
	MITEM(child)->highlight = false;
	jwidget_dirty(child);
      }
    }
  }

  if (menuitem) {
    if (!MITEM(menuitem)->highlight) {
      MITEM(menuitem)->highlight = true;
      jwidget_dirty(menuitem);
    }

    /* highlight parents */
    if (MENU(menu)->menuitem != NULL) {
      set_highlight(MENU(menu)->menuitem->parent,
		    MENU(menu)->menuitem, false, false, false);
    }

    /* open submenu of the menitem */
    if (HAS_SUBMENU(menuitem)) {
      if (open_submenu) {
	/* if the submenu is closed, open it */
	if (MITEM(menuitem)->submenu_menubox == NULL)
	  open_menuitem(menuitem, select_first_child);

	/* the mouse was clicked */
	get_base(menuitem)->was_clicked = true;
      }
    }
    /* execute menuitem action */
    else if (click) {
      close_all(menu);
      exe_menuitem(menuitem);
    }
  }
}

static void unhighlight(JWidget menu)
{
  set_highlight(menu, NULL, false, false, false);
}

static void open_menuitem(JWidget menuitem, bool select_first)
{
  JWidget menu;
  JWidget child;
  JMessage msg;
  JLink link;
  Base *base;

  assert_valid_widget(menuitem);
  assert(HAS_SUBMENU(menuitem));

  menu = menuitem->parent;

  /* the menu item is already opened? */
  assert(MITEM(menuitem)->submenu_menubox == NULL);

  assert_valid_widget(menu);

  /* close all siblings of 'menuitem' */
  if (menu->parent) {
    JI_LIST_FOR_EACH(menu->children, link) {
      child = reinterpret_cast<JWidget>(link->data);

      if (child->type != JI_MENUITEM)
	continue;

      if (child != menuitem && MITEM(child)->submenu_menubox) {
	close_menuitem(child, false);
      }
    }
  }

  msg = jmessage_new(JM_OPEN_MENUITEM);
  msg->user.a = select_first;
  jmessage_add_dest(msg, menuitem);
  jmanager_enqueue_message(msg);

  /* get the 'base' */
  base = get_base(menuitem);
  assert(base != NULL);

  /* reset flags */
  base->close_all = false;
  base->is_processing = true;  

  /* we need to add a filter of the JM_BUTTONPRESSED to intercept
     clicks outside the menu (and close all the hierarchy in that
     case); the widget to intercept messages is the base menu-bar or
     popuped menu-box  */
  if (!base->is_filtering) {
    base->is_filtering = true;
    jmanager_add_msg_filter(JM_BUTTONPRESSED, get_base_menubox(menuitem));
  }
}

static void close_menuitem(JWidget menuitem, bool last_of_close_chain)
{
  JWidget menu, child;
  JMessage msg;
  JLink link;
  Base *base;

  assert_valid_widget(menuitem);
  assert(MITEM(menuitem)->submenu_menubox != NULL);

  /* first: recursively close the children */
  menu = jmenubox_get_menu(MITEM(menuitem)->submenu_menubox);
  assert(menu != NULL);

  JI_LIST_FOR_EACH(menu->children, link) {
    child = reinterpret_cast<JWidget>(link->data);

    if (child->type != JI_MENUITEM)
      continue;
    
    if (MITEM(child)->submenu_menubox) {
      close_menuitem(reinterpret_cast<JWidget>(link->data), false);
    }
  }

  /* second: now we can close the 'menuitem' */
  msg = jmessage_new(JM_CLOSE_MENUITEM);
  msg->user.a = last_of_close_chain;
  jmessage_add_dest(msg, menuitem);
  jmanager_enqueue_message(msg);

  /* get the 'base' */
  base = get_base(menuitem);
  assert(base != NULL);

  /* start processing */
  base->is_processing = true;
}

static void close_popup(JWidget menubox)
{
  JMessage msg = jmessage_new(JM_CLOSE_POPUP);
  jmessage_add_dest(msg, menubox);
  jmanager_enqueue_message(msg);
}

static void close_all(JWidget menu)
{
  JWidget base_menubox = NULL;
  JWidget menuitem = NULL;
  JLink link;
  Base *base;

  assert(menu != NULL);

  while (MENU(menu)->menuitem != NULL) {
    menuitem = MENU(menu)->menuitem;
    menu = menuitem->parent;
  }

  base_menubox = get_base_menubox(menu->parent);
  base = MBOX(base_menubox)->base;
  base->close_all = true;
  base->was_clicked = false;
  if (base->is_filtering) {
    base->is_filtering = false;
    jmanager_remove_msg_filter(JM_BUTTONPRESSED, base_menubox);
  }

  unhighlight(menu);

  if (menuitem != NULL) {
    if (MITEM(menuitem)->submenu_menubox != NULL)
      close_menuitem(menuitem, true);
  }
  else {
    JI_LIST_FOR_EACH(menu->children, link) {
      menuitem = reinterpret_cast<JWidget>(link->data);

      if (menuitem->type != JI_MENUITEM)
	continue;

      if (MITEM(menuitem)->submenu_menubox != NULL)
	close_menuitem(menuitem, true);
    }
  }

  /* for popuped menus */
  if (base_menubox->type == JI_MENUBOX) {
    close_popup(base_menubox);
  }
}

static void exe_menuitem(JWidget menuitem)
{
  /* send the message */
  JMessage msg = jmessage_new(JM_EXE_MENUITEM);
  jmessage_add_dest(msg, menuitem);
  jmanager_enqueue_message(msg);
}

static bool window_msg_proc(JWidget widget, JMessage msg)
{
  switch (msg->type) {

    case JM_CLOSE:
      jwidget_free_deferred(widget);
      break;

  }

  return false;
}

static JWidget check_for_letter(JWidget menu, int ascii)
{
  JWidget menuitem;
  JLink link;
  int c;

  JI_LIST_FOR_EACH(menu->children, link) {
    menuitem = (JWidget)link->data;

    if (menuitem->type != JI_MENUITEM)
      continue;

    if (menuitem->hasText())
      for (c=0; menuitem->getText()[c]; c++)
        if ((menuitem->getText()[c] == '&') && (menuitem->getText()[c+1] != '&'))
          if (tolower(ascii) == tolower(menuitem->getText()[c+1]))
	    return menuitem;
  }

  return NULL;
}

static JWidget check_for_accel(JWidget menu, JMessage msg)
{
  JWidget menuitem;
  JLink link;

  JI_LIST_FOR_EACH(menu->children, link) {
    menuitem = (JWidget)link->data;

    if (menuitem->type != JI_MENUITEM)
      continue;

    if (MITEM(menuitem)->submenu) {
      if ((menuitem = check_for_accel(MITEM(menuitem)->submenu, msg)))
        return menuitem;
    }
    else if (MITEM(menuitem)->accel) {
      if ((menuitem->isEnabled()) &&
	  (jaccel_check(MITEM(menuitem)->accel,
			  msg->any.shifts,
			  msg->key.ascii,
			  msg->key.scancode)))
	return menuitem;
    }
  }

  return NULL;
}

/* finds the next item of `menuitem', if `menuitem' is NULL searchs
   from the first item in `menu' */
static JWidget find_nextitem(JWidget menu, JWidget menuitem)
{
  JWidget nextitem;
  JLink link;

  if (menuitem)
    link = jlist_find(menu->children, menuitem)->next;
  else
    link = jlist_first(menu->children);

  for (; link != menu->children->end; link=link->next) {
    nextitem = (JWidget)link->data;
    if ((nextitem->type == JI_MENUITEM) && nextitem->isEnabled())
      return nextitem;
  }

  if (menuitem)
    return find_nextitem(menu, NULL);
  else
   return NULL;
}

static JWidget find_previtem(JWidget menu, JWidget menuitem)
{
  JWidget nextitem;
  JLink link;

  if (menuitem)
    link = jlist_find(menu->children, menuitem)->prev;
  else
    link = jlist_last(menu->children);

  for (; link != menu->children->end; link=link->prev) {
    nextitem = (JWidget)link->data;
    if ((nextitem->type == JI_MENUITEM) && nextitem->isEnabled())
      return nextitem;
  }

  if (menuitem)
    return find_previtem(menu, NULL);
  else
    return NULL;
}
