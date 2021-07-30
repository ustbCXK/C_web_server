#!/usr/bin/perl -Tw

use strict;
use CGI;

my($cgi) = new CGI;

print $cgi->header('text/html');
print $cgi->start_html(-title => "这是一个web_server例程",
                       -BGCOLOR => 'blue');
print $cgi->h1("CGI执行");
print $cgi->p, "这是一个web_server例程\n";
print $cgi->p, "输入的用户名为：\n";
print "<UL>\n";
print "<LI>", "$param \n", $cgi->param('username'), "\n";
print "</UL>";
print $cgi->end_html, "\n";
