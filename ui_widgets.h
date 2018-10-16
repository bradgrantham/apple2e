#ifndef _UI_WIDGETS_H_
#define _UI_WIDGETS_H_

#include <utility>
#include <vector>
#include <tuple>

typedef std::pair<float, float> width_height;

struct widget
{
    virtual width_height get_min_dimensions() const = 0;
    virtual void draw(double now, float to_screen[9], float x, float y, float w, float h) {};
    virtual bool click(double now, float x, float y) { return false; };
    virtual void hover(double now, float x, float y) {};
    virtual void drag(double now, float x, float y) {};
    virtual void release(double now, float x, float y) {};
    virtual bool drop(double now, float x, float y, int count, const char** paths) { return false; };
};

struct switcher
{
    float w, h;
    int which;
    std::vector<widget*> children;
    switcher(std::vector<widget*> children_) :
        w(0),
        h(0),
        which(0),
        children(children_)
    {
        for(auto it : children) {
            float cw, ch;
            std::tie(cw, ch) = it->get_min_dimensions();
            w = std::max(w, cw);
            h = std::max(h, ch);
        }
    }
    virtual width_height get_min_dimensions() const 
    {
        return {w, h};
    }
    virtual void draw(double now, float to_screen[9], float x, float y, float w, float h)
    {
        children[which]->draw(now, to_screen, x, y, w, h);
    }
    virtual bool click(double now, float x, float y)
    {
        return children[which]->click(now, x, y);
    }
    virtual void hover(double now, float x, float y)
    {
        children[which]->hover(now, x, y);
    }
    virtual void drag(double now, float x, float y)
    {
        children[which]->drag(now, x, y);
    }
    virtual void release(double now, float x, float y)
    {
        children[which]->release(now, x, y);
    }
    virtual bool drop(double now, float x, float y, int count, const char** paths)
    {
        return children[which]->drop(now, x, y, count, paths);
    }
};

struct spacer : public widget
{
    float w, h;
    spacer(float w_, float h_) :
        w(w_),
        h(h_)
    {}
    virtual width_height get_min_dimensions() const 
    {
        return {w, h};
    }
};

struct placed_widget
{
    widget *widg;
    float x, y, w, h;
};

struct padding : public widget
{
    widget* child;
    float w, h;
    float left_pad, right_pad, top_pad, bottom_pad;
    float cw, ch;

    padding(float left_pad_, float right_pad_, float top_pad_, float bottom_pad_, widget* child_) :
        child(child_),
        left_pad(left_pad_),
        right_pad(right_pad_),
        top_pad(top_pad_),
        bottom_pad(bottom_pad_)
    {
        std::tie(cw, ch) = child->get_min_dimensions();
        w = cw + left_pad_ + right_pad_;
        h = ch + top_pad_ + bottom_pad_;
    }

    virtual width_height get_min_dimensions() const
    {
        return {w, h};
    }
    virtual void draw(double now, float to_screen[9], float x, float y, float w_, float h_)
    {
        child->draw(now, to_screen, x + left_pad, y + top_pad, w_ - left_pad - right_pad, h_ - top_pad - bottom_pad);
    }
    virtual bool drop(double now, float x, float y, int count, const char **paths)
    {
        return child->drop(now, x + left_pad, y + top_pad, count, paths);
    }
    virtual bool click(double now, float x, float y)
    {
        return child->click(now, x + left_pad, y + top_pad);
    }
    virtual void hover(double now, float x, float y)
    {
        child->hover(now, x + left_pad, y + top_pad);
    }
    virtual void drag(double now, float x, float y)
    {
        child->drag(now, x + left_pad, y + top_pad);
    }
    virtual void release(double now, float x, float y)
    {
        child->release(now, x + left_pad, y + top_pad);
    }
};

struct centering : public widget
{
    float w, h;
    float cw, ch;
    widget* child;

    centering(widget* child_) : child(child_)
    {
        std::tie(cw, ch) = child->get_min_dimensions();
    }

    virtual width_height get_min_dimensions() const
    {
        return {cw, ch};
    }
    virtual void draw(double now, float to_screen[9], float x, float y, float w_, float h_)
    {
        w = w_;
        h = h_;
        child->draw(now, to_screen, x + (w - cw) / 2, y + (h - ch) / 2, cw, ch);
    }
    virtual bool drop(double now, float x, float y, int count, const char **paths)
    {
        return child->drop(now, x - (w - cw) / 2, y - (h - ch) / 2, count, paths);
    }
    virtual bool click(double now, float x, float y)
    {
        return child->click(now, x - (w - cw) / 2, y - (h - ch) / 2); // XXX should limit to cw,ch too
    }
    virtual void hover(double now, float x, float y)
    {
        child->hover(now, x - (w - cw) / 2, y - (h - ch) / 2);
    }
    virtual void drag(double now, float x, float y)
    {
        child->drag(now, x - (w - cw) / 2, y - (h - ch) / 2);
    }
    virtual void release(double now, float x, float y)
    {
        child->release(now, x - (w - cw) / 2, y - (h - ch) / 2);
    }
};

struct widgetbox : public widget
{
    enum Direction {VERTICAL, HORIZONTAL} dir;
    float w, h;
    std::vector<placed_widget> children;
    placed_widget focus;

    widgetbox(Direction dir_, std::vector<widget*> children_) :
        dir(dir_),
        w(0),
        h(0),
        focus({nullptr, 0, 0, 0, 0})
    {
        for(auto it : children_) {
            widget *child = it;
            float cw, ch;
            std::tie(cw, ch) = child->get_min_dimensions();
            if(dir == HORIZONTAL) {
                w += cw;
                h = std::max(h, ch);
            } else {
                w = std::max(w, cw);
                h += ch;
            }
        }
        float x = 0;
        float y = 0;
        for(auto it : children_) {
            widget *child = it;
            float cw, ch;
            std::tie(cw, ch) = child->get_min_dimensions();
            if(dir == HORIZONTAL) {
                children.push_back({child, x, y, cw, h});
                x += cw;
            } else {
                children.push_back({child, x, y, w, ch});
                y += ch;
            }
        }
    }
    virtual width_height get_min_dimensions() const
    {
        return {w, h};
    }
    virtual void draw(double now, float to_screen[9], float x, float y, float w, float h)
    {
        for(auto child : children) {
            child.widg->draw(now, to_screen, x + child.x, y + child.y, child.w, child.h);
        }
    }
    virtual bool drop(double now, float x, float y, int count, const char **paths)
    {
        for(auto child : children) {
            if(child.widg->drop(now, x - child.x, y - child.y, count, paths)) {
                return true;
            }
        }
        return false;
    }
    virtual bool click(double now, float x, float y)
    {
        for(auto child : children) {
            if(child.widg->click(now, x - child.x, y - child.y)) {
                focus = child;
                return true;
            }
        }
        return false;
    }
    virtual void hover(double now, float x, float y)
    {
        for(auto child : children) {
            if(x >= child.x && x < child.x + child.w && y >= child.y && y < child.y + child.h)
                child.widg->hover(now, x - child.x, y - child.y);
        }
    }
    virtual void drag(double now, float x, float y)
    {
        focus.widg->click(now, x - focus.x, y - focus.y);
    }
    virtual void release(double now, float x, float y)
    {
        focus.widg->release(now, x - focus.x, y - focus.y);
        focus = {nullptr, 0, 0};
    }
};

#endif /* _UI_WIDGETS_H_ */
