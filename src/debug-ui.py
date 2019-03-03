#!/usr/bin/env python

from __future__ import print_function
from flask import Flask, jsonify, redirect, request


app = Flask(__name__, static_url_path="", static_folder="")


@app.route("/")
def root():
    return redirect("index.html")


@app.route("/update", methods=["POST"])
def update():
    response = "Update Success" if request.files else "Update Error"
    return (response, 200)


@app.route("/settings", methods=["GET"])
def get_settings():
    settings = {
        "ssid": "WiFi SSID",
        "ssid-psk": "abc",
        "api-key": "123",
        "tz": "+01:00"
    }
    return jsonify(settings)


@app.route("/settings", methods=["POST"])
def save_settings():
    print(request.form)
    return ("Success", 200) if request.form['ssid'] else ("Error", 400)


if __name__ == "__main__":
    app.run(host='0.0.0.0', debug=True)
