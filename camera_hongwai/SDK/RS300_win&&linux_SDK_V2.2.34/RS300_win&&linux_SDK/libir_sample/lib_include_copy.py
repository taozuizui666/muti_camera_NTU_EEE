# -*- coding: utf-8 -*-
"""
Created on Wed Jul  8 09:06:20 2020

@author: dell
"""
import os, shutil

def copyfile(src_file, dst_path):  #将文件拷贝至dst_path目录下
    if not os.path.isfile(src_file):
        print("%s not exist!"%(src_file))
    else:
        if not os.path.exists(dst_path):
            os.makedirs(dst_path)                #创建路径
        shutil.copy(src_file,dst_path)      #复制文件
        print("copy %s -> %s"%( src_file,dst_path))


last_path=os.path.abspath(os.path.dirname(os.getcwd()))  #上级目录


libirv4l2_header=last_path+'\\libir_SDK_release\include\\libirv4l2.h'
libiruvc_header=last_path+'\\libir_SDK_release\include\\libiruvc.h'
libiruart_header=last_path+'\\libir_SDK_release\include\\libiruart.h'
libirdfu_header=last_path+'\\libir_SDK_release\include\\libirdfu.h'
libiri2c_header=last_path+'\\libir_SDK_release\include\\libiri2c.h'
src_header_list1=[ libirv4l2_header,libiruvc_header, libiruart_header,libirdfu_header,libiri2c_header]
dst_header_path1=os.getcwd()+'\drivers'

libircmd_header=last_path+'\\libir_SDK_release\include\\libircmd.h'
libircmd_temp_header=last_path+'\\libir_SDK_release\include\\libircmd_temp.h'
libircam_header=last_path+'\\libir_SDK_release\include\\libircam.h'
error_header=last_path+'\\libir_SDK_release\include\\error.h'
libirinfo_parse_header=last_path+'\\libir_SDK_release\include\\libir_infoparse.h'
src_header_list2=[libircmd_header, libircmd_temp_header, libircam_header,error_header,libirinfo_parse_header]
dst_header_path2=os.getcwd()+'\interfaces'

libirtemp_header=last_path+'\\libir_SDK_release\include\\libirtemp.h'
libirparse_header=last_path+'\\libir_SDK_release\include\\libirparse.h'
libirupgrade_header=last_path+'\\libir_SDK_release\include\\libirupgrade.h'
src_header_list3=[libirtemp_header,libirparse_header,libirupgrade_header]
dst_header_path3=os.getcwd()+'\other'

    
for i in range(len(src_header_list1)):
    copyfile(src_header_list1[i],dst_header_path1)

for i in range(len(src_header_list2)):
    copyfile(src_header_list2[i],dst_header_path2)

for i in range(len(src_header_list3)):
    copyfile(src_header_list3[i],dst_header_path3)

input("copy completed!! Press any key to exit...")
    
