import tkinter as tk

class window:
    def __init__(self, title="TkNix", geometry="400x400"):
        self.root = tk.Tk()
        self.root.title(title)
        self.root.geometry(geometry)
        self.widgets = []

    def run(self):
        self.root.mainloop()

    def frame(self, parent=None):
        f = tk.Frame(parent if parent else self.root)
        f.pack()
        self.widgets.append(f)
        return f

    def label(self, parent, text="", pady=5, padx=5):
        l = tk.Label(parent, text=text)
        l.pack(pady=pady, padx=padx)
        self.widgets.append(l)
        return l

    def button(self, parent, text="button", command=None, relief="raised", pady=5, padx=5):
        b = tk.Button(parent, text=text, command=command, relief=relief)
        b.pack(pady=pady, padx=padx)
        self.widgets.append(b)
        return b

    def entry(self, parent, pady=5, padx=5):
        e = tk.Entry(parent)
        e.pack(pady=pady, padx=padx)
        self.widgets.append(e)
        return e

    def text(self, parent, height=5, width=30):
        t = tk.Text(parent, height=height, width=width)
        t.pack()
        self.widgets.append(t)
        return t

    def checkbox(self, parent, text=""):
        var = tk.BooleanVar()
        c = tk.Checkbutton(parent, text=text, variable=var)
        c.pack()
        self.widgets.append(c)
        return var

    def radiobutton(self, parent, text, value, variable):
        r = tk.Radiobutton(parent, text=text, value=value, variable=variable)
        r.pack()
        self.widgets.append(r)
        return r

    def listbox(self, parent, items=None):
        lb = tk.Listbox(parent)
        if items:
            for i in items:
                lb.insert(tk.END, i)
        lb.pack()
        self.widgets.append(lb)
        return lb

    def scale(self, parent, from_=0, to=100, orient="horizontal"):
        s = tk.Scale(parent, from_=from_, to=to, orient=orient)
        s.pack()
        self.widgets.append(s)
        return s

    def canvas(self, parent, width=200, height=200):
        c = tk.Canvas(parent, width=width, height=height)
        c.pack()
        self.widgets.append(c)
        return c

    def clear(self):
        for w in self.widgets:
            try:
                w.destroy()
            except:
                pass
        self.widgets = []

    def set_bg(self, color):
        self.root.configure(bg=color)

    def set_title(self, title):
        self.root.title(title)

    def set_geometry(self, geometry):
        self.root.geometry(geometry)

    def after(self, delay, func):
        self.root.after(delay, func)

    def quit(self):
        self.root.quit()

    def destroy(self):
        self.root.destroy()


if __name__ == "__main__":
    app = window()

    f = app.frame()
    app.label(f, "Hello")
    e = app.entry(f)

    def show_text():
        app.label(f, e.get())

    app.button(f, "Print", show_text)
    app.checkbox(f, "Check me")
    app.scale(f)
    app.listbox(f, ["A", "B", "C"])

    app.run()