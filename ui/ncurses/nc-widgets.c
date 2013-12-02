/*
 *  Copyright (C) 2013 IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define _GNU_SOURCE

#include "config.h"

#include <linux/input.h> /* This must be included before ncurses.h */
#if defined HAVE_NCURSESW_CURSES_H
#  include <ncursesw/curses.h>
#elif defined HAVE_NCURSESW_H
#  include <ncursesw.h>
#elif defined HAVE_NCURSES_CURSES_H
#  include <ncurses/curses.h>
#elif defined HAVE_NCURSES_H
#  include <ncurses.h>
#elif defined HAVE_CURSES_H
#  include <curses.h>
#else
#  error "Curses header file not found."
#endif

#if defined HAVE_NCURSESW_FORM_H
#  include <ncursesw/form.h>
#elif defined HAVE_NCURSES_FORM_H
#  include <ncurses/form.h>
#elif defined HAVE_FORM_H
#  include <form.h>
#else
#  error "Curses form.h not found."
#endif

#include <string.h>
#include <ctype.h>

#include <talloc/talloc.h>
#include <types/types.h>
#include <log/log.h>
#include <util/util.h>

#include "config.h"
#include "nc-cui.h"
#include "nc-widgets.h"

#undef move

#define to_checkbox(w) container_of(w, struct nc_widget_checkbox, widget)
#define to_textbox(w) container_of(w, struct nc_widget_textbox, widget)
#define to_button(w) container_of(w, struct nc_widget_button, widget)
#define to_select(w) container_of(w, struct nc_widget_select, widget)

static const char *checkbox_checked_str = "[*]";
static const char *checkbox_unchecked_str = "[ ]";

static const char *select_selected_str = "(*)";
static const char *select_unselected_str = "( )";

struct nc_widgetset {
	WINDOW	*mainwin;
	WINDOW	*subwin;
	FORM	*form;
	FIELD	**fields;
	int	n_fields, n_alloc_fields;
	void	(*widget_focus)(struct nc_widget *, void *);
	void	*widget_focus_arg;
	FIELD	*cur_field;
};

struct nc_widget {
	FIELD	*field;
	bool	(*process_key)(struct nc_widget *, FORM *, int);
	void	(*set_visible)(struct nc_widget *, bool);
	void	(*move)(struct nc_widget *, int, int);
	void	(*field_focus)(struct nc_widget *, FIELD *);
	int	focussed_attr;
	int	unfocussed_attr;
	int	height;
	int	width;
	int	focus_y;
	int	x;
	int	y;
};

struct nc_widget_label {
	struct nc_widget	widget;
	const char		*text;
};

struct nc_widget_checkbox {
	struct nc_widget	widget;
	bool			checked;
};

struct nc_widget_textbox {
	struct nc_widget	widget;
};

struct nc_widget_select {
	struct nc_widget	widget;
	struct select_option {
		char		*str;
		int		val;
		FIELD		*field;
	} *options;
	int			top, left, size;
	int			n_options, selected_option;
	struct nc_widgetset	*set;
	void			(*on_change)(void *, int);
	void			*on_change_arg;
};

struct nc_widget_button {
	struct nc_widget	widget;
	void			(*click)(void *arg);
	void			*arg;
};

static void widgetset_add_field(struct nc_widgetset *set, FIELD *field);
static void widgetset_remove_field(struct nc_widgetset *set, FIELD *field);

static bool key_is_select(int key)
{
	return key == ' ' || key == '\r' || key == '\n' || key == KEY_ENTER;
}

static bool process_key_nop(struct nc_widget *widget __attribute__((unused)),
		FORM *form __attribute((unused)),
		int key __attribute__((unused)))
{
	return false;
}

static void field_set_visible(FIELD *field, bool visible)
{
	int opts = field_opts(field) & ~O_VISIBLE;
	if (visible)
		opts |= O_VISIBLE;
	set_field_opts(field, opts);
}

static void field_move(FIELD *field, int y, int x)
{
	move_field(field, y, x);
}

static int label_destructor(void *ptr)
{
	struct nc_widget_label *label = ptr;
	free_field(label->widget.field);
	return 0;
}


struct nc_widget_label *widget_new_label(struct nc_widgetset *set,
		int y, int x, char *str)
{
	struct nc_widget_label *label;
	FIELD *f;
	int len;

	len = strlen(str);

	label = talloc_zero(set, struct nc_widget_label);
	label->widget.height = 1;
	label->widget.width = len;
	label->widget.x = x;
	label->widget.y = y;
	label->widget.process_key = process_key_nop;
	label->widget.field = f = new_field(1, len, y, x, 0, 0);
	label->widget.focussed_attr = A_NORMAL;
	label->widget.unfocussed_attr = A_NORMAL;

	field_opts_off(f, O_ACTIVE);
	set_field_buffer(f, 0, str);
	set_field_userptr(f, &label->widget);

	widgetset_add_field(set, label->widget.field);
	talloc_set_destructor(label, label_destructor);

	return label;
}

bool widget_checkbox_get_value(struct nc_widget_checkbox *checkbox)
{
	return checkbox->checked;
}

static void checkbox_set_buffer(struct nc_widget_checkbox *checkbox)
{
	const char *str;
	str = checkbox->checked ? checkbox_checked_str : checkbox_unchecked_str;
	set_field_buffer(checkbox->widget.field, 0, str);
}

static bool checkbox_process_key(struct nc_widget *widget,
		FORM *form __attribute__((unused)), int key)
{
	struct nc_widget_checkbox *checkbox = to_checkbox(widget);

	if (!key_is_select(key))
		return false;

	checkbox->checked = !checkbox->checked;
	checkbox_set_buffer(checkbox);

	return true;
}

static int checkbox_destructor(void *ptr)
{
	struct nc_widget_checkbox *checkbox = ptr;
	free_field(checkbox->widget.field);
	return 0;
}

struct nc_widget_checkbox *widget_new_checkbox(struct nc_widgetset *set,
		int y, int x, bool checked)
{
	struct nc_widget_checkbox *checkbox;
	FIELD *f;

	checkbox = talloc_zero(set, struct nc_widget_checkbox);
	checkbox->checked = checked;
	checkbox->widget.height = 1;
	checkbox->widget.width = strlen(checkbox_checked_str);
	checkbox->widget.x = x;
	checkbox->widget.y = y;
	checkbox->widget.process_key = checkbox_process_key;
	checkbox->widget.focussed_attr = A_REVERSE;
	checkbox->widget.unfocussed_attr = A_NORMAL;
	checkbox->widget.field = f = new_field(1, strlen(checkbox_checked_str),
			y, x, 0, 0);

	field_opts_off(f, O_EDIT);
	set_field_userptr(f, &checkbox->widget);
	checkbox_set_buffer(checkbox);

	widgetset_add_field(set, checkbox->widget.field);
	talloc_set_destructor(checkbox, checkbox_destructor);

	return checkbox;
}

static char *strip_string(char *str)
{
	int len, i;

	len = strlen(str);

	/* clear trailing space */
	for (i = len - 1; i >= 0; i--) {
		if (!isspace(str[i]))
			break;
		str[i] = '\0';
	}

	/* increment str past leading space */
	for (i = 0; i < len; i++) {
		if (str[i] == '\0' || !isspace(str[i]))
			break;
	}

	return str + i;
}

char *widget_textbox_get_value(struct nc_widget_textbox *textbox)
{
	char *str = field_buffer(textbox->widget.field, 0);
	return str ? strip_string(str) : NULL;
}

static bool textbox_process_key(
		struct nc_widget *widget __attribute__((unused)),
		FORM *form, int key)
{
	switch (key) {
	case KEY_HOME:
		form_driver(form, REQ_BEG_FIELD);
		break;
	case KEY_END:
		form_driver(form, REQ_END_FIELD);
		break;
	case KEY_LEFT:
		form_driver(form, REQ_LEFT_CHAR);
		break;
	case KEY_RIGHT:
		form_driver(form, REQ_RIGHT_CHAR);
		break;
	case KEY_BACKSPACE:
		if (form_driver(form, REQ_LEFT_CHAR) != E_OK)
			break;
		/* fall through */
	case KEY_DC:
		form_driver(form, REQ_DEL_CHAR);
		break;
	default:
		form_driver(form, key);
		break;
	}

	return true;
}

static int textbox_destructor(void *ptr)
{
	struct nc_widget_textbox *textbox = ptr;
	free_field(textbox->widget.field);
	return 0;
}

struct nc_widget_textbox *widget_new_textbox(struct nc_widgetset *set,
		int y, int x, int len, char *str)
{
	struct nc_widget_textbox *textbox;
	FIELD *f;

	textbox = talloc_zero(set, struct nc_widget_textbox);
	textbox->widget.height = 1;
	textbox->widget.width = len;
	textbox->widget.x = x;
	textbox->widget.y = y;
	textbox->widget.process_key = textbox_process_key;
	textbox->widget.field = f = new_field(1, len, y, x, 0, 0);
	textbox->widget.focussed_attr = A_REVERSE;
	textbox->widget.unfocussed_attr = A_UNDERLINE;

	field_opts_off(f, O_STATIC | O_WRAP | O_BLANK);
	set_field_buffer(f, 0, str);
	set_field_back(f, textbox->widget.unfocussed_attr);
	set_field_userptr(f, &textbox->widget);

	widgetset_add_field(set, textbox->widget.field);
	talloc_set_destructor(textbox, textbox_destructor);

	return textbox;
}

static void select_option_change(struct select_option *opt, bool selected)
{
	const char *str;

	str = selected ? select_selected_str : select_unselected_str;

	memcpy(opt->str, str, strlen(str));
	set_field_buffer(opt->field, 0, opt->str);
}

static bool select_process_key(struct nc_widget *w, FORM *form, int key)
{
	struct nc_widget_select *select = to_select(w);
	struct select_option *new_opt, *old_opt;
	int i, new_idx;
	FIELD *field;

	if (!key_is_select(key))
		return false;

	field = current_field(form);
	new_opt = NULL;

	for (i = 0; i < select->n_options; i++) {
		if (select->options[i].field == field) {
			new_opt = &select->options[i];
			new_idx = i;
			break;
		}
	}

	if (!new_opt)
		return true;

	if (new_idx == select->selected_option)
		return true;

	old_opt = &select->options[select->selected_option];

	select_option_change(old_opt, false);
	select_option_change(new_opt, true);

	select->selected_option = new_idx;

	if (select->on_change)
		select->on_change(select->on_change_arg, new_opt->val);

	return true;
}

static void select_set_visible(struct nc_widget *widget, bool visible)
{
	struct nc_widget_select *select = to_select(widget);
	int i;

	for (i = 0; i < select->n_options; i++)
		field_set_visible(select->options[i].field, visible);
}

static void select_move(struct nc_widget *widget, int y, int x)
{
	struct nc_widget_select *select = to_select(widget);
	int i;

	for (i = 0; i < select->n_options; i++)
		field_move(select->options[i].field, y + i, x);
}

static void select_field_focus(struct nc_widget *widget, FIELD *field)
{
	struct nc_widget_select *select = to_select(widget);
	int i;

	for (i = 0; i < select->n_options; i++) {
		if (field != select->options[i].field)
			continue;
		widget->focus_y = i;
		return;
	}
}

static int select_destructor(void *ptr)
{
	struct nc_widget_select *select = ptr;
	int i;

	for (i = 0; i < select->n_options; i++)
		free_field(select->options[i].field);

	return 0;
}

struct nc_widget_select *widget_new_select(struct nc_widgetset *set,
		int y, int x, int len)
{
	struct nc_widget_select *select;

	select = talloc_zero(set, struct nc_widget_select);
	select->widget.width = len;
	select->widget.height = 0;
	select->widget.x = x;
	select->widget.y = y;
	select->widget.process_key = select_process_key;
	select->widget.set_visible = select_set_visible;
	select->widget.move = select_move;
	select->widget.field_focus = select_field_focus;
	select->widget.focussed_attr = A_REVERSE;
	select->widget.unfocussed_attr = A_NORMAL;
	select->top = y;
	select->left = x;
	select->size = len;
	select->set = set;

	talloc_set_destructor(select, select_destructor);

	return select;
}

void widget_select_add_option(struct nc_widget_select *select, int value,
		const char *text, bool selected)
{
	const char *str;
	FIELD *f;
	int i;

	/* if we never see an option with selected set, we want the first
	 * one to be selected */
	if (select->n_options == 0)
		selected = true;
	else if (selected)
		select_option_change(&select->options[select->selected_option],
					false);

	if (selected) {
		select->selected_option = select->n_options;
		str = select_selected_str;
	} else
		str = select_unselected_str;

	i = select->n_options++;
	select->widget.height = select->n_options;

	select->options = talloc_realloc(select, select->options,
				struct select_option, i + 2);
	select->options[i].val = value;
	select->options[i].str = talloc_asprintf(select->options,
					"%s %s", str, text);

	select->options[i].field = f = new_field(1, select->size,
						select->top + i,
						select->left, 0, 0);

	field_opts_off(f, O_WRAP | O_EDIT);
	set_field_userptr(f, &select->widget);
	set_field_buffer(f, 0, select->options[i].str);

	widgetset_add_field(select->set, f);
}

int widget_select_get_value(struct nc_widget_select *select)
{
	if (!select->n_options)
		return -1;
	return select->options[select->selected_option].val;
}

int widget_select_height(struct nc_widget_select *select)
{
	return select->n_options;
}

void widget_select_on_change(struct nc_widget_select *select,
		void (*on_change)(void *, int), void *arg)
{
	select->on_change = on_change;
	select->on_change_arg = arg;
}

void widget_select_drop_options(struct nc_widget_select *select)
{
	struct nc_widgetset *set = select->set;
	int i;

	for (i = 0; i < select->n_options; i++) {
		FIELD *field = select->options[i].field;
		widgetset_remove_field(set, field);
		if (field == set->cur_field)
			set->cur_field = NULL;
		free_field(select->options[i].field);
	}

	talloc_free(select->options);
	select->options = NULL;
	select->n_options = 0;
	select->widget.height = 0;
	select->widget.focus_y = 0;

}

static bool button_process_key(struct nc_widget *widget,
		FORM *form __attribute__((unused)), int key)
{
	struct nc_widget_button *button = to_button(widget);

	if (!button->click)
		return false;

	if (!key_is_select(key))
		return false;

	button->click(button->arg);
	return true;
}

static int button_destructor(void *ptr)
{
	struct nc_widget_button *button = ptr;
	free_field(button->widget.field);
	return 0;
}

struct nc_widget_button *widget_new_button(struct nc_widgetset *set,
		int y, int x, int size, const char *str,
		void (*click)(void *), void *arg)
{
	struct nc_widget_button *button;
	char *text;
	FIELD *f;
	int idx, len;

	button = talloc_zero(set, struct nc_widget_button);
	button->widget.height = 1;
	button->widget.width = size;
	button->widget.x = x;
	button->widget.y = y;
	button->widget.field = f = new_field(1, size + 2, y, x, 0, 0);
	button->widget.process_key = button_process_key;
	button->widget.focussed_attr = A_REVERSE;
	button->widget.unfocussed_attr = A_NORMAL;
	button->click = click;
	button->arg = arg;

	field_opts_off(f, O_EDIT);
	set_field_userptr(f, &button->widget);

	/* center str in a size-char buffer, but don't overrun */
	len = strlen(str);
	len = min(len, size);
	idx = (size - len) / 2;

	text = talloc_array(button, char, size + 3);
	memset(text, ' ', size + 2);
	memcpy(text + idx + 1, str, len);
	text[0] = '[';
	text[size + 1] = ']';
	text[size + 2] = '\0';

	set_field_buffer(f, 0, text);

	widgetset_add_field(set, button->widget.field);
	talloc_set_destructor(button, button_destructor);

	return button;
}

static void widget_focus_change(struct nc_widget *widget, FIELD *field,
		bool focussed)
{
	int attr = focussed ? widget->focussed_attr : widget->unfocussed_attr;
	set_field_back(field, attr);
}

bool widgetset_process_key(struct nc_widgetset *set, int key)
{
	struct nc_widget *widget;
	FIELD *field;
	int req = 0;

	field = current_field(set->form);
	assert(field);

	/* handle field change events */
	switch (key) {
	case KEY_BTAB:
	case KEY_UP:
		req = REQ_PREV_FIELD;
		break;
	case '\t':
	case KEY_DOWN:
		req = REQ_NEXT_FIELD;
		break;
	case KEY_PPAGE:
		req = REQ_FIRST_FIELD;
		break;
	case KEY_NPAGE:
		req = REQ_LAST_FIELD;
		break;
	}

	widget = field_userptr(field);
	if (req) {
		widget_focus_change(widget, field, false);
		form_driver(set->form, req);
		form_driver(set->form, REQ_END_FIELD);
		field = current_field(set->form);
		widget = field_userptr(field);
		widget_focus_change(widget, field, true);
		if (widget->field_focus)
			widget->field_focus(widget, field);
		if (set->widget_focus)
			set->widget_focus(widget, set->widget_focus_arg);
		return true;
	}

	if (!widget->process_key)
		return false;

	return widget->process_key(widget, set->form, key);
}

static int widgetset_destructor(void *ptr)
{
	struct nc_widgetset *set = ptr;
	free_form(set->form);
	return 0;
}

struct nc_widgetset *widgetset_create(void *ctx, WINDOW *main, WINDOW *sub)
{
	struct nc_widgetset *set;

	set = talloc_zero(ctx, struct nc_widgetset);
	set->n_alloc_fields = 8;
	set->mainwin = main;
	set->subwin = sub;
	set->fields = talloc_array(set, FIELD *, set->n_alloc_fields);
	talloc_set_destructor(set, widgetset_destructor);

	return set;
}

void widgetset_set_windows(struct nc_widgetset *set,
		WINDOW *main, WINDOW *sub)
{
	set->mainwin = main;
	set->subwin = sub;
}

void widgetset_set_widget_focus(struct nc_widgetset *set,
		widget_focus_cb cb, void *arg)
{
	set->widget_focus = cb;
	set->widget_focus_arg = arg;
}

void widgetset_post(struct nc_widgetset *set)
{
	struct nc_widget *widget;
	FIELD *field;

	set->form = new_form(set->fields);
	set_form_win(set->form, set->mainwin);
	set_form_sub(set->form, set->subwin);
	post_form(set->form);
	form_driver(set->form, REQ_END_FIELD);

	if (set->cur_field) {
		set_current_field(set->form, set->cur_field);
		field = set->cur_field;
	}

	field = current_field(set->form);
	widget = field_userptr(field);
	widget_focus_change(widget, field, true);
	if (set->widget_focus)
		set->widget_focus(widget, set->widget_focus_arg);
}

void widgetset_unpost(struct nc_widgetset *set)
{
	set->cur_field = current_field(set->form);
	unpost_form(set->form);
	free_form(set->form);
	set->form = NULL;
}

static void widgetset_add_field(struct nc_widgetset *set, FIELD *field)
{
	if (set->n_fields == set->n_alloc_fields - 1) {
		set->n_alloc_fields *= 2;
		set->fields = talloc_realloc(set, set->fields,
				FIELD *, set->n_alloc_fields);
	}

	set->n_fields++;
	set->fields[set->n_fields - 1] = field;
	set->fields[set->n_fields] = NULL;
}

static void widgetset_remove_field(struct nc_widgetset *set, FIELD *field)
{
	int i;

	for (i = 0; i < set->n_fields; i++) {
		if (set->fields[i] == field)
			break;
	}

	if (i == set->n_fields)
		return;

	memmove(&set->fields[i], &set->fields[i+i],
			(set->n_fields - i) * sizeof(set->fields[i]));
	set->n_fields--;
}

#define DECLARE_BASEFN(type) \
	struct nc_widget *widget_ ## type ## _base		\
		(struct nc_widget_ ## type *w)			\
	{ return &w->widget; }

DECLARE_BASEFN(textbox);
DECLARE_BASEFN(checkbox);
DECLARE_BASEFN(select);
DECLARE_BASEFN(label);
DECLARE_BASEFN(button);

void widget_set_visible(struct nc_widget *widget, bool visible)
{
	if (widget->set_visible)
		widget->set_visible(widget, visible);
	else
		field_set_visible(widget->field, visible);
}

void widget_move(struct nc_widget *widget, int y, int x)
{
	if (widget->move)
		widget->move(widget, y, x);
	else
		field_move(widget->field, y, x);

	widget->x = x;
	widget->y = y;
}

int widget_height(struct nc_widget *widget)
{
	return widget->height;
}

int widget_width(struct nc_widget *widget)
{
	return widget->width;
}

int widget_x(struct nc_widget *widget)
{
	return widget->x;
}

int widget_y(struct nc_widget *widget)
{
	return widget->y;
}

int widget_focus_y(struct nc_widget *widget)
{
	return widget->focus_y;
}
