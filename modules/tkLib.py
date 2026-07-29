import tkinter as tk

def Window(title="TkNix", geometry="400x400"):
	root = tk.Tk()
	root.title(title)
	root.geometry(geometry)


def TkButton(w, text="button", commmand=None, relief="sunken", pady=20, padx=20):
    b = tk.Button(w, text=text, commmand=commmand, relief=relief)
    b.pack(pady=pady, padx=padx)