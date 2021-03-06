/** Handling of gexf and gexf.gz temporal graph output files.
 *
 *  \file
 *
 *  e.g. to be used by Gephi
 *
 *  In the gexf output files, each entity is represented by a node,
 *  and each time interval at which a link existed is represented by
 *  a directed edge whose id, start and end attributes encode the time interval.
 *  Symmetric relationships/actions are thus represented by two directed edges.
 *
 *  Depending on the analysis performed, edges might have to be "merged"
 *  to a single edge representing all time intervals of the link at once in Gephi.
 *  However, since gephi does not sufficiently support multiplex graphs,
 *  this merging will also merge links of different relationship or action type
 *  between the same source and target entities into one link.
 *
 *  By default, all relationship or action types go to the main output file,
 *  but via the config file, individual types can also be suppressed or
 *  redirected to other files.
 *  (Note that gephi allows uniting several files into one workspace.)
 */

#include "global_variables.h"
#include "gexf.h"

// tell boost header files to use gzip and zlib instead of their own libs:
#define BOOST_IOSTREAMS_NO_LIB
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/gzip.hpp>

unordered_map<string, bool> gexf_is_gz;  ///< whether an output file is gzip-compressed
unordered_map<string, ofstream> gexf_uncompressed, gexf_compressed;  ///< streams for uncompressed and compressed output
unordered_map<string, boost::iostreams::filtering_streambuf<boost::iostreams::output>> gexf_buf;  ///< buffer for compressed output
ostream* gexf(NULL);  ///< actual stream to write to

unordered_map<tricl::tricllink, timepoint> gexf_edge2start = {};  ///< Time of establishment of edge

/** \returns the stream to write to.
 *
 *  NOTE: for some reason this apparently cannot be stored permanently in a global variable between writes.
 */
inline ostream* get_stream (string fn, bool is_gz)
{
    if (is_gz) {
        ostream gexf_gz(&(gexf_buf[fn]));
        gexf = (ostream*)(&gexf_gz);
    } else {
        gexf = (ostream*)(&(gexf_uncompressed[fn]));
    }
    return gexf;
}

/** Prepare gexf output.
 *
 *  Opens files, streams, buffers and outputs file headers with all entities.
 */
void init_gexf ()
{
    if (verbose) cout << " prepare gexf output files:" << endl;
    // open files:
    for (auto& [rat13, fn] : gexf_filename) {
        if ((fn != "") && (gexf_is_gz.count(fn)==0)) {
            // get file extension:
            auto ext = fn.substr(fn.find_last_of(".") + 1);
            if (ext == "gexf")
            {
                // uncompressed
                gexf_is_gz[fn] = false;
                gexf_uncompressed[fn].open(fn);
            }
            else if (ext == "gz")
            {
                // compressed
                gexf_is_gz[fn] = true;
                gexf_compressed[fn] = ofstream(fn, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
                // prepare boost's compression buffer:
                gexf_buf[fn].push(boost::iostreams::gzip_compressor());
                gexf_buf[fn].push(gexf_compressed[fn]);
            }
            else throw "files:gexf must end with .gexf or .gexf.gz";
        }
    }

    // output all nodes into all files:
    for (auto& [fn, is_gz] : gexf_is_gz) {
        if (fn != "") {
            if (verbose) cout << "  " << fn << endl;
            gexf = get_stream(fn, is_gz);
            // give all attributes one-letter ids so that they can be accessed
            // as global variables in Gephi's python scripting plugin:
//            *gexf << R"V0G0N(<?xml version="1.0" encoding="UTF-8"?>
//<gexf xmlns="http://www.gexf.net/1.2draft" version="1.2" xmlns:viz="http://www.gexf.net/1.1draft/viz">
//    <meta>
//        <creator>tricl</creator>
//        <description>dynamic graph generated by tricl model</description>
//    </meta>
//    <graph mode="dynamic" defaultedgetype="directed">
//        <attributes class="node">
//            <attribute id="T" title="entity type" type="string"/>
//        </attributes>
//        <attributes class="edge">
//            <attribute id="R" title="relationship or action type" type="string"/>
//            <attribute id="S" title="start" type="float"/>
//            <attribute id="E" title="end" type="float"/>
//        </attributes>
//        <nodes>
//)V0G0N";
            *gexf << R"V0G0N(<?xml version="1.0" encoding="UTF-8"?>
<!--0--> <gexf xmlns="http://www.gexf.net/1.2draft" version="1.2" xmlns:viz="http://www.gexf.net/1.1draft/viz"><meta><creator>tricl</creator><description>dynamic graph generated by tricl model</description></meta><graph mode="dynamic" defaultedgetype="directed"><attributes class="node"><attribute id="T" title="entity type" type="string"/></attributes><attributes class="edge"><attribute id="R" title="relationship or action type" type="string"/><attribute id="S" title="start" type="float"/><attribute id="E" title="end" type="float"/></attributes><nodes>)V0G0N";
            for (auto& e : es) {
                auto et = e2et[e];
                *gexf << "<node id=\"" << e << "\" label=\"" << e2label[e]
                     << "\" start=\"0.0\" end=\"" << max_t
                     << "\"><attvalues><attvalue for=\"T\" value=\""
                     << et2label[et] << "\"/></attvalues>";
                // output visualization information:
                if (et2gexf_size.count(et) > 0) *gexf
                     << "<viz:size value=\"" << et2gexf_size[et] << "\"/>";
                if (et2gexf_shape.count(et) > 0) *gexf
                     << "<viz:shape value=\"" << et2gexf_shape[et] << "\"/>";
                if (et2gexf_r.count(et) > 0) *gexf
                     << "<viz:color r=\"" << et2gexf_r[et] << "\" g=\"" << et2gexf_g[et] << "\" b=\"" << et2gexf_b[et] << "\" a=\"" << et2gexf_a[et] << "\"/>";
                *gexf << "</node>" ;
            }
//            *gexf << R"V0G0N(
//        </nodes>
//        <edges>
//)V0G0N";
            *gexf << R"V0G0N(</nodes><edges>)V0G0N";
        }
    }
}

/** Write a single edge to the proper file.
 *
 *  The unique edge id is <et1>_<rat13>_<et3>_<n_events>.
 *  Start and end time are stored both in the "start"/"end" xml attributes
 *  so that gephi's timeline works properly,
 *  as well as in gexf "attributes" named "S"/"E"
 *  so that one can access them as global variables in gephi's python scripting plugin.
 */
void gexf_output_edge (tricl::tricllink& l) {
    auto e1 = l.e1, e3 = l.e3;
    auto rat13 = l.rat13;
    if (rat13 != RT_ID) {
        string fn = gexf_filename[rat13];
        if (fn != "") {
            if (verbose) cout << "    writing link to " << fn << endl;
            gexf = get_stream(fn, gexf_is_gz[fn]);
            double start = gexf_edge2start.at(l), end = current_t;
            *gexf << "<!--"  << e1 << "_" << rat13 << "_" << e3 << "_" << n_events
                  << "--> <edge id=\"" << e1 << "_" << rat13 << "_" << e3 << "_" << n_events
                  << "\" source=\"" << e1
                  << "\" target=\"" << e3
                  << "\" start=\"" << start
                  << "\" end=\"" << end
                  << "\"><attvalues><attvalue for=\"R\" value=\"" << rat2label[rat13]
                  << "\"/><attvalue for=\"S\" value=\"" << start
                  << "\"/><attvalue for=\"E\" value=\"" << end
                  << "\"/></attvalues>";
            if (rat2gexf_thickness.count(rat13) > 0) *gexf
                  << "<viz:thickness value=\"" << rat2gexf_thickness[rat13] << "\"/>";
            if (rat2gexf_shape.count(rat13) > 0) *gexf
                  << "<viz:shape value=\"" << rat2gexf_shape[rat13] << "\"/>";
            if (rat2gexf_r.count(rat13) > 0) *gexf
                  << "<viz:color r=\"" << rat2gexf_r[rat13] << "\" g=\"" << rat2gexf_g[rat13] << "\" b=\"" << rat2gexf_b[rat13] << "\" a=\"" << rat2gexf_a[rat13] << "\"/>";
            *gexf << "</edge>" << endl;
        }
    }
    if (gexf_edge2start.erase(l) != 1) throw "missing link start time";
}

/** Complete and close all output files.
 */
void finish_gexf () {

    // write edges for still existing links:
    if (verbose) cout << " write surviving links to gexf output files" << endl;
    bool old_verbose = verbose;
    verbose = false;
    current_t = max_t;  // TODO: is this correct/neccessary/helpful?
    for (auto& [e1, outs] : e2outs) {
        for (auto& [rat13, e3] : outs) {
            tricl::tricllink l = { .e1 = e1, .rat13 = rat13, .e3 = e3 };
            if (rat13 != RT_ID) gexf_output_edge(l);
        }
    }
    verbose = old_verbose;

    // output footer to all files:
    if (!quiet) cout << " complete gexf output files:" << endl;
    for (auto& [fn, is_gz] : gexf_is_gz) {
        if (fn != "") {
            if (!quiet) cout << "  " << fn << endl;
            gexf = get_stream(fn, is_gz);
//            *gexf << R"V0G0N(        </edges>
//    </graph>
//</gexf>
//)V0G0N";
            *gexf << R"V0G0N(<!--Z--> </edges></graph></gexf>)V0G0N" << endl;
            if (!is_gz) gexf_uncompressed[fn].close();
        }
    }
}

