#!/usr/bin/env python

from __future__ import print_function
from flask import Flask, redirect, request


app = Flask(__name__, static_url_path="", static_folder="")


@app.route("/")
def root():
    return redirect("index.html")


@app.route("/update", methods=["POST"])
def update():
    response = "Update Error" if len(request.files) == 0 else "Update Success"
    return response, 200


@app.route("/settings", methods=["POST"])
def settings():
    print(request.form)
    return ("Success", 200) if request.form['ssid'] else ("Error", 400)


if __name__ == "__main__":
    app.run(debug=True)
