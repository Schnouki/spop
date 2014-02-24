/*
 * Copyright (C) 2014 Thomas Jost
 * This file is part of spop and is Licensed under the GPLv3. See COPYING for
 * details.
 */

define(["backbone", "underscore"],
function(Backbone,   _) {
    var Image = Backbone.Model.extend({
        url: "/api/image",

        initialize: function() {
            _.bindAll(this, "fetch");
            this.on("sync", this.synced, this);
        },

        synced: function() {
            if (this.get("status") === "not-loaded") {
                window.setTimeout(this.fetch, 50);
            }
        }
    });

    return Image;
});
