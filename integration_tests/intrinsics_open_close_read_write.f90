program open_close_read_write  
implicit none   

real, dimension(100) :: x, y  
real, dimension(100) :: p, q
integer :: i  

! data  
do i = 1,100  
    x(i) = i * 0.1 
    y(i) = sin(x(i)) * (1-cos(x(i)/3.0))  
end do  

! output data into a file 
open(1, file = 'data1.dat', status='new')  
do i = 1,100  
    write(1,*) x(i), y(i)   
end do  
close(1) 

! opening the file for reading
open (2, file = 'data1.dat', status = 'old')

do i = 1,100  
    read(2,*) p(i), q(i)
end do 

close(2)

do i = 1,100  
    write(*,*) p(i), q(i)
end do 
       
end program