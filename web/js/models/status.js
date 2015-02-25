/*
 * Copyright (C) 2014 Thomas Jost
 * This file is part of spop and is Licensed under the GPLv3. See COPYING for
 * details.
 */

define(["backbone", "underscore", "models/image"],
function(Backbone,   _,            ImageModel) {
    var Status = Backbone.Model.extend({
        defaults: {
            album: null,
            artist: null,
            current_track: null,
            img: null,
            title: null
        },
        url: "/api/status",
        poll_url: "/api/idle",
        model_image: null,

        initialize: function() {
            _.bindAll(this, "poll", "poll_and_fetch", "image_updated");

            var model_image = new ImageModel();
            model_image.on("change:data", this.image_updated);
            this.on("change:uri", function() {
                model_image.fetch();
            });

            this.poll_and_fetch();
        },

        poll: function() {
            this.fetch({
                url: this.poll_url,
                complete: this.poll_and_fetch
            });
        },

        poll_and_fetch: function() {
            this.poll();
            this.fetch();
        },

        image_updated: function(model, img_data) {
            this.set("img", img_data);
        },

        toggle_repeat: function() {
            Backbone.$.get("/api/repeat");
        },
        toggle_shuffle: function() {
            Backbone.$.get("/api/shuffle");
        },

        play: function() {
            Backbone.$.get("/api/toggle");
        },
        stop: function() {
            Backbone.$.get("/api/stop");
        },
        prev: function() {
            Backbone.$.get("/api/prev");
        },
        next: function() {
            Backbone.$.get("/api/next");
        }
    });

    return Status;
});
